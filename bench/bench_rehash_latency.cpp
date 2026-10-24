#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include "store/hash_table.h"

#ifdef __linux__
#include <unistd.h>
static size_t get_peak_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            size_t kb = 0;
            if (sscanf(line.c_str(), "VmHWM: %zu kB", &kb) == 1) {
                return kb;
            }
        }
    }
    return 0;
}
static size_t get_current_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            size_t kb = 0;
            if (sscanf(line.c_str(), "VmRSS: %zu kB", &kb) == 1) {
                return kb;
            }
        }
    }
    return 0;
}
#else
static size_t get_peak_rss_kb() { return 0; }
static size_t get_current_rss_kb() { return 0; }
#endif

struct LatencyStats {
    double p50_us{0};
    double p90_us{0};
    double p99_us{0};
    double p999_us{0};
    double max_us{0};
    double avg_us{0};
    double total_ms{0};
};

static LatencyStats compute_stats(std::vector<double>& latencies) {
    if (latencies.empty()) return {};
    std::vector<double> sorted = latencies;
    std::sort(sorted.begin(), sorted.end());

    LatencyStats stats;
    size_t n = sorted.size();
    stats.p50_us = sorted[static_cast<size_t>(n * 0.50)];
    stats.p90_us = sorted[static_cast<size_t>(n * 0.90)];
    stats.p99_us = sorted[static_cast<size_t>(n * 0.99)];
    stats.p999_us = sorted[static_cast<size_t>(n * 0.999)];
    stats.max_us = sorted.back();

    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    stats.avg_us = sum / n;
    stats.total_ms = sum / 1000.0;
    return stats;
}

int main(int argc, char** argv) {
    size_t total_keys = 1000000;
    if (argc > 1) {
        total_keys = std::stoull(argv[1]);
    }

    std::cout << "===============================================================\n";
    std::cout << "  BENCHMARK: Incremental Rehashing vs Stop-the-World Baseline\n";
    std::cout << "  Total Operations: " << total_keys << " unique key-value insertions\n";
    std::cout << "===============================================================\n\n";

    // Pre-generate keys to prevent string formatting overhead from polluting measurement
    std::cout << "[1/4] Pre-generating " << total_keys << " test keys and values...\n";
    std::vector<std::string> keys;
    std::vector<std::string> values;
    keys.reserve(total_keys);
    values.reserve(total_keys);

    for (size_t i = 0; i < total_keys; ++i) {
        char kbuf[64];
        char vbuf[64];
        snprintf(kbuf, sizeof(kbuf), "benchmark_key_%08zu", i);
        snprintf(vbuf, sizeof(vbuf), "value_%08zu", i);
        keys.emplace_back(kbuf);
        values.emplace_back(vbuf);
    }
    std::cout << "      Pre-generation complete.\n\n";

    // ------------------------------------------------------------------------
    // BENCHMARK 1: Stop-the-World Rehashing
    // ------------------------------------------------------------------------
    std::cout << "[2/4] Benchmarking Stop-the-World Resize (" << total_keys << " insertions)...\n";
    std::vector<double> stw_latencies;
    stw_latencies.reserve(total_keys);

    {
        redis_clone::store::HashTable ht;
        ht.set_incremental_rehash(false);  // Force Stop-the-World resize
        ht.set_auto_resize(true);

        for (size_t i = 0; i < total_keys; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            ht.set(keys[i], values[i]);
            auto end = std::chrono::high_resolution_clock::now();

            double elapsed_us = std::chrono::duration<double, std::micro>(end - start).count();
            stw_latencies.push_back(elapsed_us);
        }
    }
    std::cout << "      Stop-the-World benchmark completed.\n\n";

    // ------------------------------------------------------------------------
    // BENCHMARK 2: Incremental Rehashing
    // ------------------------------------------------------------------------
    std::cout << "[3/4] Benchmarking Incremental Rehashing (" << total_keys << " insertions)...\n";
    std::vector<double> inc_latencies;
    inc_latencies.reserve(total_keys);

    size_t ht_peak_rss_kb = 0;
    {
        redis_clone::store::HashTable ht;
        ht.set_incremental_rehash(true);  // Enable Incremental Rehashing (default)
        ht.set_auto_resize(true);

        for (size_t i = 0; i < total_keys; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            ht.set(keys[i], values[i]);
            auto end = std::chrono::high_resolution_clock::now();

            double elapsed_us = std::chrono::duration<double, std::micro>(end - start).count();
            inc_latencies.push_back(elapsed_us);
        }
        ht_peak_rss_kb = get_current_rss_kb();
    }
    std::cout << "      Incremental Rehashing benchmark completed.\n\n";

    // ------------------------------------------------------------------------
    // BENCHMARK 3: std::unordered_map Memory Comparison
    // ------------------------------------------------------------------------
    std::cout << "[4/4] Benchmarking std::unordered_map memory baseline (" << total_keys << " insertions)...\n";
    size_t map_peak_rss_kb = 0;
    {
        std::unordered_map<std::string, std::string> map;
        for (size_t i = 0; i < total_keys; ++i) {
            map[keys[i]] = values[i];
        }
        map_peak_rss_kb = get_current_rss_kb();
    }
    std::cout << "      Memory benchmark completed.\n\n";

    // ------------------------------------------------------------------------
    // STATISTICAL ANALYSIS
    // ------------------------------------------------------------------------
    LatencyStats stw_stats = compute_stats(stw_latencies);
    LatencyStats inc_stats = compute_stats(inc_latencies);

    std::cout << "========================================================================\n";
    std::cout << "                      BENCHMARK RESULTS & METRICS                       \n";
    std::cout << "========================================================================\n\n";

    std::cout << "| Metric                      | Stop-the-World     | Incremental Rehash | Improvement       |\n";
    std::cout << "|-----------------------------|--------------------|--------------------|-------------------|\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "| p50 Latency (median)        | " << std::setw(15) << stw_stats.p50_us << " us | "
              << std::setw(15) << inc_stats.p50_us << " us | "
              << std::setw(14) << (stw_stats.p50_us / (inc_stats.p50_us > 0 ? inc_stats.p50_us : 1.0)) << "x |\n";
    std::cout << "| p90 Latency                 | " << std::setw(15) << stw_stats.p90_us << " us | "
              << std::setw(15) << inc_stats.p90_us << " us | "
              << std::setw(14) << (stw_stats.p90_us / (inc_stats.p90_us > 0 ? inc_stats.p90_us : 1.0)) << "x |\n";
    std::cout << "| p99 Latency                 | " << std::setw(15) << stw_stats.p99_us << " us | "
              << std::setw(15) << inc_stats.p99_us << " us | "
              << std::setw(14) << (stw_stats.p99_us / (inc_stats.p99_us > 0 ? inc_stats.p99_us : 1.0)) << "x |\n";
    std::cout << "| p99.9 Latency               | " << std::setw(15) << stw_stats.p999_us << " us | "
              << std::setw(15) << inc_stats.p999_us << " us | "
              << std::setw(14) << (stw_stats.p999_us / (inc_stats.p999_us > 0 ? inc_stats.p999_us : 1.0)) << "x |\n";
    std::cout << "| Max Latency (Peak Spike)    | " << std::setw(12) << stw_stats.max_us / 1000.0 << " ms | "
              << std::setw(12) << inc_stats.max_us / 1000.0 << " ms | "
              << std::setw(14) << (stw_stats.max_us / (inc_stats.max_us > 0 ? inc_stats.max_us : 1.0)) << "x reduction |\n";
    std::cout << "| Average Latency             | " << std::setw(15) << stw_stats.avg_us << " us | "
              << std::setw(15) << inc_stats.avg_us << " us | "
              << std::setw(14) << (stw_stats.avg_us / (inc_stats.avg_us > 0 ? inc_stats.avg_us : 1.0)) << "x |\n";
    std::cout << "| Total Ingestion Time        | " << std::setw(14) << stw_stats.total_ms << " ms | "
              << std::setw(14) << inc_stats.total_ms << " ms | "
              << std::setw(17) << "-" << " |\n\n";

    if (ht_peak_rss_kb > 0 && map_peak_rss_kb > 0) {
        double ht_bytes_per_key = (static_cast<double>(ht_peak_rss_kb) * 1024.0) / total_keys;
        double map_bytes_per_key = (static_cast<double>(map_peak_rss_kb) * 1024.0) / total_keys;

        std::cout << "========================================================================\n";
        std::cout << "                      MEMORY FOOTPRINT AT " << total_keys << " KEYS                      \n";
        std::cout << "========================================================================\n\n";
        std::cout << "| Data Structure              | Peak RSS (MB)        | Bytes Per Key      |\n";
        std::cout << "|-----------------------------|----------------------|--------------------|\n";
        std::cout << "| redis_clone::HashTable      | " << std::setw(17) << ht_peak_rss_kb / 1024.0 << " MB | "
                  << std::setw(15) << ht_bytes_per_key << " B/key |\n";
        std::cout << "| std::unordered_map          | " << std::setw(17) << map_peak_rss_kb / 1024.0 << " MB | "
                  << std::setw(15) << map_bytes_per_key << " B/key |\n\n";
    }


    // ------------------------------------------------------------------------
    // EXPORT TIME-SERIES CSV FOR PLOTTING
    // ------------------------------------------------------------------------
    std::cout << "Exporting time-series latency data to bench/rehash_latency.csv...\n";
    std::ofstream csv("bench/rehash_latency.csv");
    csv << "op_index,stw_latency_us,inc_latency_us\n";

    // Sample every step or when a latency spike occurs (> 50 us)
    size_t sample_stride = total_keys / 2000;
    if (sample_stride == 0) sample_stride = 1;

    for (size_t i = 0; i < total_keys; ++i) {
        bool is_sample = (i % sample_stride == 0);
        bool is_stw_spike = (stw_latencies[i] > 50.0);
        bool is_inc_spike = (inc_latencies[i] > 50.0);

        if (is_sample || is_stw_spike || is_inc_spike || i == total_keys - 1) {
            csv << i << "," << stw_latencies[i] << "," << inc_latencies[i] << "\n";
        }
    }
    csv.close();
    std::cout << "Time-series CSV exported successfully.\n\n";

    // ------------------------------------------------------------------------
    // EXPORT SUMMARY METRICS CSV
    // ------------------------------------------------------------------------
    std::ofstream summary_csv("bench/rehash_summary.csv");
    summary_csv << "metric,stw_val,inc_val\n";
    summary_csv << "p50_us," << stw_stats.p50_us << "," << inc_stats.p50_us << "\n";
    summary_csv << "p90_us," << stw_stats.p90_us << "," << inc_stats.p90_us << "\n";
    summary_csv << "p99_us," << stw_stats.p99_us << "," << inc_stats.p99_us << "\n";
    summary_csv << "p999_us," << stw_stats.p999_us << "," << inc_stats.p999_us << "\n";
    summary_csv << "max_us," << stw_stats.max_us << "," << inc_stats.max_us << "\n";
    summary_csv << "avg_us," << stw_stats.avg_us << "," << inc_stats.avg_us << "\n";
    summary_csv << "total_ms," << stw_stats.total_ms << "," << inc_stats.total_ms << "\n";
    summary_csv.close();

    std::cout << "Benchmark run complete. All results saved.\n";
    return 0;
}
