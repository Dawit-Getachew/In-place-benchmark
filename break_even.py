# break_even.py — Compute break-even M (ops after init) where sec3/sec4 overtake a baseline
# Model: total_time = init_time + M * ns_per_op
# M* = (init_baseline - init_impl) / max(ns_per_op_impl - ns_per_op_baseline, eps)
# Usage:
#   python break_even.py --scenario WRITE_RANDOM --baseline std_vector --Ns 10000,100000,1000000, 100000000
#   python break_even.py --scenario MIXED_R50W50
import argparse, math, os
import pandas as pd

def load_agg():
    if not os.path.exists("aggregate.csv"):
        raise SystemExit("aggregate.csv not found. Run analyze_results.py first.")
    return pd.read_csv("aggregate.csv")

def pick(df, impl, scenario, N):
    r = df[(df.impl_name==impl) & (df.scenario==scenario) & (df.N==N)]
    return float(r['ns_per_op'].iloc[0]) if not r.empty else float('nan')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", required=True, help="e.g., WRITE_RANDOM, WRITE_SEQUENTIAL, MIXED_R50W50")
    ap.add_argument("--baseline", default="std_vector", help="baseline impl")
    ap.add_argument("--impls", default="sec4,sec3", help="comma list of impls to compare against baseline")
    ap.add_argument("--Ns", default="auto", help="comma sizes or 'auto' to use all in aggregate.csv")
    args = ap.parse_args()

    agg = load_agg()
    Ns = sorted(agg['N'].dropna().unique().astype(int).tolist()) if args.Ns=="auto" else [int(x) for x in args.Ns.split(",") if x]
    impls = [s.strip() for s in args.impls.split(",") if s.strip()]

    init_times = {}
    for impl in [args.baseline] + impls:
        init_times[impl] = { int(N): pick(agg, impl, "INIT_ONLY", int(N)) for N in Ns }

    rows = []
    for N in Ns:
        base_init = init_times[args.baseline].get(N, float('nan'))
        base_op = pick(agg, args.baseline, args.scenario, N)
        for impl in impls:
            impl_init = init_times[impl].get(N, float('nan'))
            impl_op = pick(agg, impl, args.scenario, N)
            if any(math.isnan(x) for x in [base_init, base_op, impl_init, impl_op]):
                M_be = float('nan')
            else:
                delta_init = base_init - impl_init
                delta_op = impl_op - base_op
                eps = 1e-9
                if delta_op <= 0:
                    M_be = 0.0 if delta_init < 0 else float('inf')
                else:
                    M_be = max(0.0, delta_init / delta_op)
            rows.append({"N": int(N), "scenario": args.scenario, "baseline": args.baseline,
                         "impl": impl, "break_even_ops": M_be})
    out = pd.DataFrame(rows).sort_values(["scenario","N","impl"])
    out.to_csv("break_even.csv", index=False, encoding="utf-8")
    print("Break-even ops (M*) where total_time_impl <= total_time_baseline for: scenario =", args.scenario)
    for N in Ns:
        print(f"\nN={N}:")
        for impl in impls:
            M_be = out[(out.N==N)&(out.impl==impl)]['break_even_ops'].iloc[0]
            if math.isinf(M_be): print(f"  {impl}: per-op is faster and init cheaper -> wins for all M")
            elif math.isnan(M_be): print(f"  {impl}: insufficient data in aggregate.csv")
            else: print(f"  {impl}: M* ≈ {M_be:,.0f} ops after init")

if __name__ == "__main__":
    main()
