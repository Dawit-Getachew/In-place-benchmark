# In-place-benchmark
> In-place Initializable Array Benchmark

# Steps to Reproduce the results

## 0) Python env

```bash
# create env
python -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
```

---

## 1) C++ — in-place algorithm + benchmark

Section 3 (block=2) and Section 4 (block=4) are built into the same binary.

**Build**

```bash
g++ -O3 -std=c++17 -DNDEBUG main.cpp -o benchmark
```

**Run (writes C++ results to results.csv)**

```bash
./benchmark
```

> Windows: use `g++` from MSYS2/MinGW, or build with MSVC (`cl /O2 /std:c++17 main.cpp`).

---

## 2) Python baseline

```bash
# writes python-results.csv
python py_benchmark.py --Ns 10000,100000,1000000,10000000,100000000 --reps 3 --seed 42 --outfile python-results.csv
```

---

## 3) Go baseline

```bash
# writes go-results.csv
go run go_benchmark.go -Ns 10000,100000,1000000,10000000,100000000 -reps 3 -seed 42 -outfile go-results.csv
```

---

## 4) Rust baseline

> Windows: install the MSVC toolchain (`rustup default stable-x86_64-pc-windows-msvc`) and the “Desktop development with C++” workload so `link.exe` is available.

```bash
# writes rust-results.csv
cargo run --release --manifest-path rust_benchmark/Cargo.toml -- \
  --Ns 1k,10k,100k,1m,10m,100m --reps 3 --seed 42 --outfile rust-results.csv
```

---

## 5) Merge + plots

```bash
# reads: results.csv, python-results.csv, go-results.csv, rust-results.csv
# writes: aggregate.csv, plots/*.png, auto_summary.md
python analyze_results.py --baseline std_vector --dpi 220
```

---

## 6) Break-even tables

```bash
# WRITE_RANDOM break-even vs std::vector across N
python break_even.py --scenario WRITE_RANDOM --baseline std_vector \
  --Ns 10000,100000,1000000,10000000,100000000

# MIXED 50/50 break-even
python break_even.py --scenario MIXED_R50W50 --baseline std_vector \
  --Ns 10000,100000,1000000,10000000,100000000
```

---
## Machine used
> HP Omen 15, 16GB Ram, RTX3060 GPU, Windows 11

## Artifacts produced

* `results.csv` (C++), `python-results.csv`, `go-results.csv`, `rust-results.csv`
* `aggregate.csv`
* `plots/*.png`
* `auto_summary.md`
---
