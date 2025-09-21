// rust_benchmark/src/main.rs
// Usage:
//   cargo run --release --manifest-path rust_benchmark/Cargo.toml -- --Ns 1k,10k,100k,1m,10m,100m --reps 3 --seed 42 --outfile rust-results.csv

use chrono::Utc;
use csv::Writer;
use rand::{Rng, SeedableRng};
use rand::rngs::StdRng;
use std::env;
use std::hint::black_box;
use std::time::Instant;

fn now_iso() -> String { Utc::now().to_rfc3339() }

trait ArrayImpl {
    fn name(&self) -> &'static str;
    fn init(&mut self, v: i64) -> i64;
    fn read(&self, i: usize) -> i64;
    fn write(&mut self, i: usize, v: i64);
}

struct VecImpl { n: usize, a: Vec<i64> }
impl VecImpl { fn new(n: usize) -> Self { Self { n, a: vec![0; n] } } }
impl ArrayImpl for VecImpl {
    fn name(&self) -> &'static str { "rust_vec_i64" }
    fn init(&mut self, v: i64) -> i64 {
        let t0 = Instant::now();
        for i in 0..self.n { self.a[i] = v; }
        t0.elapsed().as_nanos() as i64
    }
    fn read(&self, i: usize) -> i64 { self.a[i] }
    fn write(&mut self, i: usize, v: i64) { self.a[i] = v; }
}

fn parse_sizes(s: &str) -> Vec<usize> {
    let mut out = Vec::new();
    for mut p in s.split(',') {
        if p.is_empty() { continue }
        let mut mult = 1.0_f64;
        if p.ends_with('k') || p.ends_with('K') { p = &p[..p.len()-1]; mult = 1e3; }
        if p.ends_with('m') || p.ends_with('M') { p = &p[..p.len()-1]; mult = 1e6; }
        if p.ends_with('g') || p.ends_with('G') { p = &p[..p.len()-1]; mult = 1e9; }
        if let Ok(v) = p.parse::<f64>() { out.push((v*mult) as usize); }
    }
    out
}

fn rand_val(rng: &mut StdRng) -> i64 { (rng.gen_range(0..2001) as i64) - 1000 }

fn mk_idx(rng: &mut StdRng, m: usize, n: usize) -> Vec<usize> {
    (0..m).map(|_| rng.gen_range(0..n)).collect()
}

fn run_scenario(arr: &mut dyn ArrayImpl, scenario: &str, n: usize, seed: u64) -> (usize, i64, f64, i64) {
    let mut rng = StdRng::seed_from_u64(seed);

    match scenario {
        "INIT_ONLY" => {
            let t0 = Instant::now();
            arr.init(42);
            let el = t0.elapsed().as_nanos() as i64;
            (1, el, 0.0, el)
        }
        "READ_UNWRITTEN" => {
            arr.init(123);
            let m = std::cmp::min(1_000_000usize, 10*n);
            let idx = mk_idx(&mut rng, m, n);
            let t0 = Instant::now();
            let mut s: i64 = 0;
            for &j in &idx { s = s.wrapping_add(arr.read(black_box(j))); }
            let el = t0.elapsed().as_nanos() as i64;
            black_box(s);
            (m, el, el as f64 / m as f64, 0)
        }
        "WRITE_SEQUENTIAL" => {
            arr.init(0);
            let t0 = Instant::now();
            for i in 0..n { arr.write(i, i as i64); }
            let el = t0.elapsed().as_nanos() as i64;
            (n, el, el as f64 / n as f64, 0)
        }
        "WRITE_RANDOM" => {
            arr.init(0);
            let m = std::cmp::min(1_000_000usize, n);
            let idx = mk_idx(&mut rng, m, n);
            let t0 = Instant::now();
            for &j in &idx { arr.write(black_box(j), rand_val(&mut rng)); }
            let el = t0.elapsed().as_nanos() as i64;
            (m, el, el as f64 / m as f64, 0)
        }
        s if s.starts_with("MIXED_") => {
            let p = &s[6..];
            let rpos = p.find('R').unwrap();
            let wpos = p.find('W').unwrap();
            let read_pct: i32 = p[(rpos+1)..wpos].parse().unwrap();

            arr.init(42);
            let m = std::cmp::min(1_000_000usize, n);
            let idx = mk_idx(&mut rng, m, n);
            let ops: Vec<u8> = (0..m).map(|_| if rng.gen_range(0..100) < read_pct {0} else {1}).collect();

            let t0 = Instant::now();
            let mut ssum: i64 = 0;
            for t in 0..m {
                if ops[t] == 0 { ssum = ssum.wrapping_add(arr.read(black_box(idx[t]))); }
                else { arr.write(black_box(idx[t]), rand_val(&mut rng)); }
            }
            let el = t0.elapsed().as_nanos() as i64;
            black_box(ssum);
            (m, el, el as f64 / m as f64, 0)
        }
        "ADVERSARIAL_HOTSPOT" => {
            arr.init(0);
            let m = std::cmp::min(1_000_000usize, n);
            let hot = std::cmp::max(1usize, n/10);
            let t0 = Instant::now();
            for _ in 0..m {
                let j = if rng.gen_range(0..2) == 0 { rng.gen_range(0..hot) } else { rng.gen_range(0..n) };
                arr.write(black_box(j), rand_val(&mut rng));
            }
            let el = t0.elapsed().as_nanos() as i64;
            (m, el, el as f64 / m as f64, 0)
        }
        _ => panic!("unknown scenario"),
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut Ns = String::from("10000,100000,1000000");
    let mut reps: usize = 3;
    let mut seed: u64 = 42;
    let mut outfile = String::from("rust-results.csv");

    let mut args = env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--Ns" => if let Some(v) = args.next() { Ns = v },
            "--reps" => if let Some(v) = args.next() { reps = v.parse().unwrap_or(3) },
            "--seed" => if let Some(v) = args.next() { seed = v.parse().unwrap_or(42) },
            "--outfile" => if let Some(v) = args.next() { outfile = v },
            _ => {},
        }
    }

    let mut wtr = Writer::from_path(outfile.clone())?;
    wtr.write_record(&[
        "timestamp_iso","impl_name","scenario","N","seed","rep_id",
        "ops_in_run","total_time_ns","ns_per_op","init_time_ns_if_recorded",
        "relocations_count","conversions_count",
    ])?;

    let n_list = {
        let v = parse_sizes(&Ns);
        if v.is_empty() { vec![10_000usize, 100_000, 1_000_000] } else { v }
    };
    let seeds = vec![seed];
    let scenarios = vec![
        "INIT_ONLY","READ_UNWRITTEN","WRITE_SEQUENTIAL","WRITE_RANDOM",
        "MIXED_R90W10","MIXED_R80W20","MIXED_R70W30","MIXED_R50W50","MIXED_R30W70","MIXED_R10W90",
        "ADVERSARIAL_HOTSPOT",
    ];

    for &n in &n_list {
        for s in &scenarios {
            for &seed in &seeds {
                for rep in 1..=reps {
                    let mut arr = VecImpl::new(n);
                    let (ops, tot, nspop, initns) = run_scenario(&mut arr, s, n, seed);
                    wtr.write_record(&[
                        now_iso(), arr.name().to_string(), s.to_string(),
                        format!("{}", n), format!("{}", seed), format!("{}", rep),
                        format!("{}", ops), format!("{}", tot), format!("{:.4}", nspop),
                        format!("{}", initns), "0".to_string(), "0".to_string()
                    ])?;
                }
            }
        }
    }
    wtr.flush()?;
    println!("Wrote {}", outfile);
    Ok(())
}
