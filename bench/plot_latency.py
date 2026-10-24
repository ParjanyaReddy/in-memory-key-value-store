import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

def generate_plots(csv_path="bench/rehash_latency.csv", summary_path="bench/rehash_summary.csv", output_path="bench/latency_comparison.png"):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        sys.exit(1)

    print(f"Loading data from {csv_path}...")
    indices = []
    stw_latencies = []
    inc_latencies = []

    with open(csv_path, "r") as f:
        header = f.readline()
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) >= 3:
                indices.append(int(parts[0]))
                stw_latencies.append(float(parts[1]))
                inc_latencies.append(float(parts[2]))

    indices = np.array(indices)
    stw_latencies = np.array(stw_latencies)
    inc_latencies = np.array(inc_latencies)

    # Load summary metrics
    summary = {}
    if os.path.exists(summary_path):
        with open(summary_path, "r") as f:
            f.readline()
            for line in f:
                parts = line.strip().split(",")
                if len(parts) >= 3:
                    summary[parts[0]] = (float(parts[1]), float(parts[2]))

    # Style configuration
    plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(13, 10), gridspec_kw={"height_ratios": [1.4, 1.0]})
    fig.patch.set_facecolor("#0f172a")

    # Panel 1: Time-series Latency
    ax1.set_facecolor("#1e293b")
    ax1.plot(indices, stw_latencies, color="#f43f5e", alpha=0.85, linewidth=1.2, label="Stop-the-World Resize (Baseline)")
    ax1.plot(indices, inc_latencies, color="#10b981", alpha=0.9, linewidth=1.2, label="Incremental Rehashing (Ours)")

    ax1.set_title("Per-Operation Latency During Ingestion to 1,000,000 Keys", fontsize=15, fontweight="bold", color="#f8fafc", pad=12)
    ax1.set_xlabel("Number of Keys Inserted", fontsize=12, fontweight="medium", color="#94a3b8")
    ax1.set_ylabel("Latency (microseconds, \u03bcs)", fontsize=12, fontweight="medium", color="#94a3b8")
    ax1.set_yscale("log")
    ax1.set_ylim(bottom=0.5, top=max(np.max(stw_latencies) * 2.0, 10000))
    ax1.tick_params(colors="#cbd5e1", labelsize=10)
    ax1.grid(True, linestyle="--", alpha=0.2, color="#94a3b8")

    # Annotate peak stop-the-world spike
    max_stw_idx = np.argmax(stw_latencies)
    max_stw_val = stw_latencies[max_stw_idx]
    ax1.annotate(
        f"Stop-the-World Peak Spike\n{max_stw_val/1000.0:.2f} ms ({max_stw_val:.0f} \u03bcs)",
        xy=(indices[max_stw_idx], max_stw_val),
        xytext=(indices[max_stw_idx] - 220000, max_stw_val * 0.4),
        arrowprops=dict(facecolor="#f43f5e", shrink=0.08, width=1.5, headwidth=7),
        fontsize=10,
        fontweight="bold",
        color="#f43f5e",
        bbox=dict(boxstyle="round,pad=0.4", fc="#0f172a", ec="#f43f5e", lw=1.2)
    )

    leg1 = ax1.legend(loc="upper left", frameon=True, facecolor="#0f172a", edgecolor="#334155", fontsize=11)
    for text in leg1.get_texts():
        text.set_color("#f8fafc")

    # Panel 2: Latency Percentiles Bar Chart
    ax2.set_facecolor("#1e293b")
    metrics = ["p50_us", "p90_us", "p99_us", "p999_us", "max_us"]
    metric_labels = ["p50 (Median)", "p90", "p99", "p99.9", "Max Spike"]

    if summary:
        stw_vals = [summary.get(m, (0, 0))[0] for m in metrics]
        inc_vals = [summary.get(m, (0, 0))[1] for m in metrics]
    else:
        stw_vals = [np.percentile(stw_latencies, 50), np.percentile(stw_latencies, 90),
                    np.percentile(stw_latencies, 99), np.percentile(stw_latencies, 99.9), np.max(stw_latencies)]
        inc_vals = [np.percentile(inc_latencies, 50), np.percentile(inc_latencies, 90),
                    np.percentile(inc_latencies, 99), np.percentile(inc_latencies, 99.9), np.max(inc_latencies)]

    x = np.arange(len(metric_labels))
    width = 0.35

    rects1 = ax2.bar(x - width/2, stw_vals, width, label="Stop-the-World", color="#f43f5e", alpha=0.9, edgecolor="#fda4af")
    rects2 = ax2.bar(x + width/2, inc_vals, width, label="Incremental Rehash", color="#10b981", alpha=0.9, edgecolor="#6ee7b7")

    ax2.set_title("Latency Distribution by Percentile (\u03bcs)", fontsize=14, fontweight="bold", color="#f8fafc", pad=12)
    ax2.set_ylabel("Latency (microseconds, \u03bcs)", fontsize=12, fontweight="medium", color="#94a3b8")
    ax2.set_xticks(x)
    ax2.set_xticklabels(metric_labels, fontsize=11, fontweight="medium", color="#cbd5e1")
    ax2.set_yscale("log")
    ax2.tick_params(colors="#cbd5e1", labelsize=10)
    ax2.grid(True, linestyle="--", alpha=0.2, color="#94a3b8")

    # Add value labels above bars
    for rect in rects1:
        h = rect.get_height()
        label = f"{h/1000.0:.1f}ms" if h >= 1000 else f"{h:.1f}\u03bcs"
        ax2.annotate(label,
                    xy=(rect.get_x() + rect.get_width() / 2, h),
                    xytext=(0, 3), textcoords="offset points",
                    ha="center", va="bottom", fontsize=8.5, color="#fda4af", fontweight="bold")

    for rect in rects2:
        h = rect.get_height()
        label = f"{h/1000.0:.1f}ms" if h >= 1000 else f"{h:.1f}\u03bcs"
        ax2.annotate(label,
                    xy=(rect.get_x() + rect.get_width() / 2, h),
                    xytext=(0, 3), textcoords="offset points",
                    ha="center", va="bottom", fontsize=8.5, color="#6ee7b7", fontweight="bold")

    leg2 = ax2.legend(loc="upper left", frameon=True, facecolor="#0f172a", edgecolor="#334155", fontsize=11)
    for text in leg2.get_texts():
        text.set_color("#f8fafc")

    plt.tight_layout(pad=2.0)
    plt.savefig(output_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    print(f"Chart successfully saved to {output_path}")

if __name__ == "__main__":
    generate_plots()
