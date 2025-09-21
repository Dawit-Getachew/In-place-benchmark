# py_benchmark.py — Python/Numpy baselines
# Writes python-results.csv.
# Usage:
#   python py_benchmark.py --Ns 10000,100000,1000000 --reps 3 --seed 42 --outfile python-results.csv
import csv, time, random, argparse
try:
    import numpy as np
except Exception:
    np = None

HEADER = ["timestamp_iso","impl_name","scenario","N","seed","rep_id",
          "ops_in_run","total_time_ns","ns_per_op","init_time_ns_if_recorded",
          "relocations_count","conversions_count"]

def now_iso():
    from datetime import datetime, UTC
    return datetime.now(UTC).isoformat()

class ArrayImpl:
    def __init__(self, N: int): self.N = N
    def name(self) -> str: raise NotImplementedError
    def init(self, v: int) -> int: raise NotImplementedError
    def read(self, i: int) -> int: raise NotImplementedError
    def write(self, i: int, v: int) -> None: raise NotImplementedError

class PyListImpl(ArrayImpl):
    def __init__(self, N): super().__init__(N); self.a = [0]*N
    def name(self): return "py_list"
    def init(self, v):
        t0 = time.perf_counter_ns(); self.a = [v]*self.N; t1 = time.perf_counter_ns(); return t1 - t0
    def read(self, i): return self.a[i]
    def write(self, i, v): self.a[i] = v

class PyArrayImpl(ArrayImpl):
    def __init__(self, N):
        import array
        super().__init__(N); self.a = array.array('q', [0])*N  
    def name(self): return "py_array_q"
    def init(self, v):
        t0 = time.perf_counter_ns()
        for i in range(self.N): self.a[i] = v
        t1 = time.perf_counter_ns()
        return t1 - t0
    def read(self, i): return self.a[i]
    def write(self, i, v): self.a[i] = v

class NumpyImpl(ArrayImpl):
    def __init__(self, N):
        super().__init__(N)
        if np is None: raise RuntimeError("numpy not available")
        self.a = np.zeros(N, dtype=np.int64)
    def name(self): return "numpy_int64"
    def init(self, v):
        t0 = time.perf_counter_ns(); self.a.fill(v); t1 = time.perf_counter_ns(); return t1 - t0
    def read(self, i): return int(self.a[i])
    def write(self, i, v): self.a[i] = v

def run_scenario(arr: ArrayImpl, scenario: str, N: int, seed: int):
    rng = random.Random(seed)
    def mk_indices(m): return [rng.randrange(0, N) for _ in range(m)]
    def rand_val(): return rng.randint(-1000, 1000)

    if scenario == "INIT_ONLY":
        t0 = time.perf_counter_ns(); arr.init(42); t1 = time.perf_counter_ns()
        return 1, (t1 - t0), 0.0, (t1 - t0)
    elif scenario == "READ_UNWRITTEN":
        arr.init(123); M = min(int(1e6), 10*N); idx = mk_indices(M)
        t0 = time.perf_counter_ns(); acc = 0
        for j in idx: acc ^= arr.read(j)
        t1 = time.perf_counter_ns(); _sink = acc
        return M, (t1 - t0), (t1 - t0) / M, 0
    elif scenario == "WRITE_SEQUENTIAL":
        arr.init(0); t0 = time.perf_counter_ns()
        for j in range(N): arr.write(j, j)
        t1 = time.perf_counter_ns(); return N, (t1 - t0), (t1 - t0) / N, 0
    elif scenario == "WRITE_RANDOM":
        arr.init(0); M = min(int(1e6), N); idx = mk_indices(M)
        t0 = time.perf_counter_ns()
        for j in idx: arr.write(j, rand_val())
        t1 = time.perf_counter_ns(); return M, (t1 - t0), (t1 - t0) / M, 0
    elif scenario.startswith("MIXED_"):
        perc = scenario[6:]; r_pos = perc.find('R'); w_pos = perc.find('W')
        read_pct = int(perc[r_pos+1:w_pos]); arr.init(42)
        M = min(int(1e6), N); idx = mk_indices(M); ops = [(0 if rng.randrange(100) < read_pct else 1) for _ in range(M)]
        t0 = time.perf_counter_ns(); acc = 0
        for op, j in zip(ops, idx):
            if op == 0: acc ^= arr.read(j)
            else: arr.write(j, rand_val())
        t1 = time.perf_counter_ns(); _sink = acc
        return M, (t1 - t0), (t1 - t0)/M, 0
    elif scenario == "ADVERSARIAL_HOTSPOT":
        arr.init(0); M = min(int(1e6), N); hotspot = max(1, N//10)
        t0 = time.perf_counter_ns()
        for _ in range(M):
            j = (rng.randrange(hotspot) if rng.randrange(2)==0 else rng.randrange(N))
            arr.write(j, rand_val())
        t1 = time.perf_counter_ns(); return M, (t1 - t0), (t1 - t0)/M, 0
    else:
        raise ValueError("unknown scenario: " + scenario)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--Ns", default="10000,100000,1000000")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--outfile", default="python-results.csv")
    args = ap.parse_args()

    N_list = [int(x) for x in args.Ns.split(",") if x]
    seeds = [args.seed]
    reps = args.reps
    scenarios = [
        "INIT_ONLY","READ_UNWRITTEN","WRITE_SEQUENTIAL","WRITE_RANDOM",
        "MIXED_R90W10","MIXED_R80W20","MIXED_R70W30","MIXED_R50W50","MIXED_R30W70","MIXED_R10W90",
        "ADVERSARIAL_HOTSPOT"
    ]
    impl_classes = [PyListImpl, PyArrayImpl] + ([NumpyImpl] if np is not None else [])

    with open(args.outfile, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(HEADER)
        for N in N_list:
            for Impl in impl_classes:
                for scenario in scenarios:
                    for rep in range(1, reps+1):
                        arr = Impl(N)
                        ops, total_ns, nspop, init_ns = run_scenario(arr, scenario, N, args.seed)
                        w.writerow([now_iso(), arr.name(), scenario, N, args.seed, rep,
                                    ops, total_ns, f"{nspop:.4f}", init_ns, 0, 0])
                        f.flush()
    print(f"Wrote {args.outfile}")

if __name__ == "__main__":
    main()
