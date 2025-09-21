# analyze_results.py — Merge C++/Python/Go/Rust CSVs and generate plots + summary.
# Usage:
#   python analyze_results.py [--baseline std_vector] [--repN auto] [--dpi 220]
#
# Outputs:
#   aggregate.csv
#   plots/*.png
#   auto_summary.md
import os
import argparse
from datetime import datetime, UTC
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

INPUTS = ["results.csv", "python-results.csv", "go-results.csv", "rust-results.csv"]

def load_all():
    dfs = []
    for path in INPUTS:
        if os.path.exists(path):
            dfs.append(pd.read_csv(path))
    if not dfs:
        raise SystemExit("No input CSVs found. Expected: " + ", ".join(INPUTS))
    df = pd.concat(dfs, ignore_index=True)
    for col in ["N","seed","rep_id","ops_in_run","total_time_ns","init_time_ns_if_recorded"]:
        if col in df.columns: df[col] = pd.to_numeric(df[col], errors="coerce")
    df["ns_per_op"] = pd.to_numeric(df["ns_per_op"], errors="coerce")
    return df

def aggregate(df: pd.DataFrame) -> pd.DataFrame:
    agg = df.groupby(['impl_name','scenario','N'], as_index=False)['ns_per_op'].median()
    agg = agg.sort_values(['scenario','N','impl_name'])
    return agg

def _annotate_bars(ax):
    for p in ax.patches:
        height = p.get_height()
        if np.isfinite(height) and height>0:
            ax.annotate(f"{height:.1f}", (p.get_x()+p.get_width()/2., height),
                        ha='center', va='bottom', fontsize=8, rotation=0, xytext=(0,2), textcoords='offset points')

def plot_repN_bars(agg, repN, dpi):
    os.makedirs("plots", exist_ok=True)
    sub = agg[agg['N']==repN]
    if sub.empty: return
    pivot = sub.pivot(index='scenario', columns='impl_name', values='ns_per_op')
    plt.figure(figsize=(13,6))
    ax = pivot.plot(kind='bar', rot=45, figsize=(13,6), legend=True)
    ax.set_title(f"Median ns/op @ N={repN}")
    ax.set_ylabel("ns per op (lower is better)")
    _annotate_bars(ax)
    plt.tight_layout(); plt.savefig(f"plots/median_ns_per_op_N{repN}.png", dpi=dpi); plt.close()

    ax = pivot.plot(kind='bar', rot=45, figsize=(13,6), legend=True, logy=True)
    ax.set_title(f"Median ns/op @ N={repN} (log scale)")
    ax.set_ylabel("ns per op (log)")
    plt.tight_layout(); plt.savefig(f"plots/median_ns_per_op_N{repN}_log.png", dpi=dpi); plt.close()

def plot_lines_vs_N(agg, scenarios, dpi):
    for scenario in scenarios:
        sub = agg[agg['scenario']==scenario]
        if sub.empty: continue
        plt.figure(figsize=(10,6))
        for impl in sub['impl_name'].unique():
            s2 = sub[sub['impl_name']==impl].sort_values('N')
            plt.plot(s2['N'], s2['ns_per_op'], marker='o', label=impl)
        plt.title(f"Median ns/op vs N — {scenario}")
        plt.xlabel("N"); plt.ylabel("ns per op"); plt.legend(); plt.grid(True, linestyle='--', linewidth=0.5)
        plt.tight_layout(); plt.savefig(f"plots/median_ns_per_op_{scenario}.png", dpi=dpi); plt.close()

def plot_speedups(agg, baseline, repN, dpi):
    sub = agg[agg['N']==repN]
    if sub.empty: return
    base = sub[sub['impl_name']==baseline].set_index('scenario')['ns_per_op']
    if base.empty: return
    rows = []
    for (impl,sc,N), val in sub.set_index(['impl_name','scenario','N'])['ns_per_op'].items():
        b = base.get(sc, np.nan)
        rows.append((sc, impl, (b/val) if (np.isfinite(b) and np.isfinite(val) and val>0 and b>0) else np.nan))
    sp = pd.DataFrame(rows, columns=['scenario','impl','speedup'])
    pivot = sp.pivot(index='scenario', columns='impl', values='speedup')
    plt.figure(figsize=(13,6))
    ax = pivot.plot(kind='bar', rot=45, figsize=(13,6), legend=True)
    ax.set_title(f"Speedup vs {baseline} @ N={repN} (higher is better)")
    ax.set_ylabel("× speedup")
    _annotate_bars(ax)
    plt.tight_layout(); plt.savefig(f"plots/speedup_vs_{baseline}_N{repN}.png", dpi=dpi); plt.close()

def write_summary(agg, dpi):
    lines = []
    lines.append(f"# Automatic Summary ({datetime.now(UTC).isoformat()})\n")
    for scenario in ["INIT_ONLY","READ_UNWRITTEN","WRITE_SEQUENTIAL","WRITE_RANDOM"]:
        sub = agg[agg['scenario']==scenario]
        if sub.empty: continue
        lines.append(f"## {scenario}\n")
        for N in sorted(sub['N'].dropna().unique().astype(int)):
            sN = sub[sub['N']==N]
            base = sN[sN['impl_name']=="std_vector"]
            if base.empty:
                lines.append(f"- N={N}: std_vector not present; skipping speedup calc.\n"); continue
            base_ns = float(base['ns_per_op'].iloc[0])
            for impl in ["sec4","sec3","numpy_int64","py_list","py_array_q","go_slice_int64","rust_vec_i64","std_vector_direct"]:
                r = sN[sN['impl_name']==impl]
                if r.empty: continue
                ns = float(r['ns_per_op'].iloc[0])
                if ns <= 0 or base_ns <= 0: continue
                speedup = base_ns / ns
                lines.append(f"- N={N}: {impl} median ns/op={ns:.1f} -> speedup vs std_vector: {speedup:.2f}x")
            lines.append("")
    with open("auto_summary.md", "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default="std_vector", help="baseline impl for speedup plots")
    ap.add_argument("--repN", default="auto", help="N for representative plots (auto picks middle)")
    ap.add_argument("--dpi", type=int, default=220)
    args = ap.parse_args()

    df = load_all()
    agg = aggregate(df)
    agg.to_csv("aggregate.csv", index=False, encoding="utf-8")

    Ns = sorted([int(n) for n in agg['N'].dropna().unique().tolist()])
    repN = Ns[min(1, len(Ns)-1)] if args.repN=="auto" else int(args.repN)

    plot_repN_bars(agg, repN, args.dpi)
    plot_lines_vs_N(agg, ["INIT_ONLY","READ_UNWRITTEN","WRITE_RANDOM","WRITE_SEQUENTIAL","MIXED_R50W50"], args.dpi)
    plot_speedups(agg, args.baseline, repN, args.dpi)
    if args.baseline != "std_vector":
        plot_speedups(agg, "std_vector", repN, args.dpi)

    write_summary(agg, args.dpi)
    print("Wrote aggregate.csv, plots/*.png, auto_summary.md")

if __name__ == "__main__":
    main()
