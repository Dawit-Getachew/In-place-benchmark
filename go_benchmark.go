//   go run go_benchmark.go -Ns 10000,100000,1000000 -reps 3 -seed 42 -outfile go-results.csv
package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"math"
	"math/rand"
	"os"
	"strconv"
	"strings"
	"time"
)

type Array interface {
	Name() string
	Init(v int64) int64
	Read(i int) int64
	Write(i int, v int64)
}

type SliceImpl struct {
	N int
	A []int64
}

func NewSliceImpl(n int) *SliceImpl { return &SliceImpl{N: n, A: make([]int64, n)} }
func (s *SliceImpl) Name() string   { return "go_slice_int64" }
func (s *SliceImpl) Init(v int64) int64 {
	start := time.Now()
	for i := 0; i < s.N; i++ {
		s.A[i] = v
	}
	elapsed := time.Since(start)
	return elapsed.Nanoseconds()
}
func (s *SliceImpl) Read(i int) int64    { return s.A[i] }
func (s *SliceImpl) Write(i int, v int64) { s.A[i] = v }

var header = []string{
	"timestamp_iso","impl_name","scenario","N","seed","rep_id",
	"ops_in_run","total_time_ns","ns_per_op","init_time_ns_if_recorded",
	"relocations_count","conversions_count",
}

func nowISO() string { return time.Now().UTC().Format(time.RFC3339) }

func min(a, b int) int { if a < b { return a } ; return b }

var sink int64

func consume(v int64) { sink ^= v }

func runScenario(arr Array, scenario string, N int, seed int64) (ops int, totalNs int64, nsPerOp float64, initNs int64) {
	rng := rand.New(rand.NewSource(seed))
	randVal := func() int64 { return int64(rng.Intn(2001) - 1000) }
	mkIdx := func(m int) []int {
		idx := make([]int, m)
		for i := 0; i < m; i++ { idx[i] = rng.Intn(N) }
		return idx
	}
	switch scenario {
	case "INIT_ONLY":
		start := time.Now()
		arr.Init(42)
		el := time.Since(start).Nanoseconds()
		return 1, el, 0, el
	case "READ_UNWRITTEN":
		arr.Init(123)
		M := min(1000000, 10*N)
		idx := mkIdx(M)
		start := time.Now()
		var s int64 = 0
		for _, j := range idx { s ^= arr.Read(j) }
		el := time.Since(start).Nanoseconds()
		consume(s)
		return M, el, float64(el)/float64(M), 0
	case "WRITE_SEQUENTIAL":
		arr.Init(0)
		start := time.Now()
		for i := 0; i < N; i++ { arr.Write(i, int64(i)) }
		el := time.Since(start).Nanoseconds()
		return N, el, float64(el)/float64(N), 0
	case "WRITE_RANDOM":
		arr.Init(0)
		M := min(1000000, N)
		idx := mkIdx(M)
		start := time.Now()
		for _, j := range idx { arr.Write(j, randVal()) }
		el := time.Since(start).Nanoseconds()
		return M, el, float64(el)/float64(M), 0
	case "MIXED_R90W10","MIXED_R80W20","MIXED_R70W30","MIXED_R50W50","MIXED_R30W70","MIXED_R10W90":
		readPct := 50
		fmt.Sscanf(scenario, "MIXED_R%dW", &readPct)
		arr.Init(42)
		M := min(1000000, N)
		idx := mkIdx(M)
		opsKind := make([]int, M) 
		for i := 0; i < M; i++ { if rng.Intn(100) < readPct { opsKind[i] = 0 } else { opsKind[i] = 1 } }
		start := time.Now()
		var s int64 = 0
		for i := 0; i < M; i++ {
			if opsKind[i] == 0 { s ^= arr.Read(idx[i]) } else { arr.Write(idx[i], randVal()) }
		}
		el := time.Since(start).Nanoseconds()
		consume(s)
		return M, el, float64(el)/float64(M), 0
	case "ADVERSARIAL_HOTSPOT":
		arr.Init(0)
		M := min(1000000, N)
		hot := int(math.Max(1, float64(N/10)))
		start := time.Now()
		for i := 0; i < M; i++ {
			var j int
			if rng.Intn(2) == 0 { j = rng.Intn(hot) } else { j = rng.Intn(N) }
			arr.Write(j, randVal())
		}
		el := time.Since(start).Nanoseconds()
		return M, el, float64(el)/float64(M), 0
	default:
		panic("unknown scenario: " + scenario)
	}
}

func parseSizes(s string) []int {
	parts := strings.Split(s, ",")
	var out []int
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p == "" { continue }
		mult := 1.0
		if strings.HasSuffix(p, "k") || strings.HasSuffix(p, "K") { p = p[:len(p)-1]; mult = 1e3 }
		if strings.HasSuffix(p, "m") || strings.HasSuffix(p, "M") { p = p[:len(p)-1]; mult = 1e6 }
		if strings.HasSuffix(p, "g") || strings.HasSuffix(p, "G") { p = p[:len(p)-1]; mult = 1e9 }
		f, err := strconv.ParseFloat(p, 64)
		if err != nil { continue }
		out = append(out, int(f*mult))
	}
	return out
}

func main() {
	NsFlag := flag.String("Ns", "10000,100000,1000000", "comma-separated sizes; supports k/m/g suffix")
	repsFlag := flag.Int("reps", 3, "repetitions")
	seedFlag := flag.Int64("seed", 42, "seed")
	outFlag := flag.String("outfile", "go-results.csv", "output csv")
	flag.Parse()

	out, err := os.Create(*outFlag)
	if err != nil { panic(err) }
	defer out.Close()
	w := csv.NewWriter(out)
	defer w.Flush()
	w.Write(header)

	Nlist := parseSizes(*NsFlag)
	if len(Nlist)==0 { Nlist = []int{10000,100000,1000000} }
	seeds := []int64{*seedFlag}
	reps := *repsFlag
	scenarios := []string{
		"INIT_ONLY","READ_UNWRITTEN","WRITE_SEQUENTIAL","WRITE_RANDOM",
		"MIXED_R90W10","MIXED_R80W20","MIXED_R70W30","MIXED_R50W50","MIXED_R30W70","MIXED_R10W90",
		"ADVERSARIAL_HOTSPOT",
	}

	for _, N := range Nlist {
		for _, scenario := range scenarios {
			for _, seed := range seeds {
				for rep := 1; rep <= reps; rep++ {
					arr := NewSliceImpl(N)
					ops, tot, nspop, initns := runScenario(arr, scenario, N, seed)
					record := []string{
						nowISO(), arr.Name(), scenario,
						fmt.Sprintf("%d", N), fmt.Sprintf("%d", seed), fmt.Sprintf("%d", rep),
						fmt.Sprintf("%d", ops), fmt.Sprintf("%d", tot), fmt.Sprintf("%.4f", nspop),
						fmt.Sprintf("%d", initns), "0","0",
					}
					if err := w.Write(record); err != nil { panic(err) }
					w.Flush()
				}
			}
		}
	}
	fmt.Printf("Wrote %s\n", *outFlag)
}
