#include "inplace.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <memory>
#include <iomanip>
#include <map>
#include <ctime>
#include <algorithm>
#include <sstream>

static std::vector<size_t> parse_sizes(const std::string& s) {
    std::vector<size_t> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        try {
            size_t mult = 1;
            if (!tok.empty() && (tok.back()=='k' || tok.back()=='K')) { mult = 1000; tok.pop_back(); }
            else if (!tok.empty() && (tok.back()=='m' || tok.back()=='M')) { mult = 1000000; tok.pop_back(); }
            else if (!tok.empty() && (tok.back()=='g' || tok.back()=='G')) { mult = 1000000000ULL; tok.pop_back(); }
            size_t val = static_cast<size_t>(std::stod(tok) * mult);
            out.push_back(val);
        } catch (...) {
            out.push_back(static_cast<size_t>(std::stoull(tok)));
        }
    }
    return out;
}
static void print_usage() {
    std::cout << "Usage:\n"
              << "  benchmark --verify <sec3|sec4> [N] [seed]\n"
              << "  benchmark [--Ns 10000,100000,1000000] [--reps 3] [--seed 42] [--impls std_vector,sec3,sec4,std_vector_direct]\n"
              << "            [--outfile results.csv]\n";
}

void verify_correctness(const std::string& impl_name, size_t N, unsigned int seed) {
    std::cout << "\n--- Running Correctness Verification for " << impl_name 
              << " with N=" << N << " seed=" << seed << " ---\n";
    
    auto reference = std::make_unique<StdVectorWrapper>(N);
    reference->enable_verification();
    
    std::unique_ptr<IInitializableArray> dut;
    
    try {
        if (impl_name == "sec3") {
            auto impl = std::make_unique<InPlaceArraySec3>(N);
            impl->enable_verification();
            dut = std::move(impl);
        } else if (impl_name == "sec4") {
            auto impl = std::make_unique<InPlaceArraySec4>(N);
            impl->enable_verification();
            dut = std::move(impl);
        } else {
            std::cerr << "Unknown impl for verification: " << impl_name << std::endl;
            return;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error creating " << impl_name << ": " << e.what() << std::endl;
        return;
    }
    
    std::mt19937 rng(seed);
    std::uniform_int_distribution<long long> value_dist(-1000, 1000);
    std::uniform_int_distribution<size_t> index_dist(0, N-1);
    
    const int num_operations = 1000;
    bool passed = true;
    
    try {
        for (int op = 0; op < num_operations && passed; op++) {
            int op_type = rng() % 3;
            
            if (op_type == 0) {  
                long long init_val = value_dist(rng);
                reference->init(init_val);
                dut->init(init_val);
                
            } else if (op_type == 1) {  
                size_t idx = index_dist(rng);
                long long ref_val = reference->read(idx);
                long long dut_val = dut->read(idx);
                
                if (ref_val != dut_val) {
                    std::cerr << "MISMATCH at read(" << idx << "): reference=" 
                              << ref_val << ", " << impl_name << "=" << dut_val << std::endl;
                    dut->dump_state_on_failure(idx);
                    passed = false;
                }
                
            } else {  
                size_t idx = index_dist(rng);
                long long val = value_dist(rng);
                reference->write(idx, val);
                dut->write(idx, val);
            }
        }
        
        if (passed && !dut->verify_correctness()) {
            passed = false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception during verification: " << e.what() << std::endl;
        passed = false;
    }
    
    if (passed) {
        std::cout << "--- Correctness Verification for " << impl_name << " PASSED ---\n\n";
    } else {
        std::cout << "--- Correctness Verification for " << impl_name << " FAILED ---\n\n";
    }
}

struct Config {
    std::string impl_name;
    std::string scenario;
    size_t N;
    unsigned int seed;
    int rep_id;
};

struct Result {
    std::string timestamp_iso;
    std::string impl_name;
    std::string scenario;
    size_t N;
    unsigned int seed;
    int rep_id;
    size_t ops_in_run = 0;
    long long total_time_ns = 0;
    double ns_per_op = 0.0;
    long long init_time_ns = 0;
    Counters counters;
};

std::string get_current_timestamp() {
    time_t now = time(0);
    char buf[sizeof "2025-09-10T11:19:27Z"];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return buf;
}

void write_csv_header(std::ofstream& file) {
    file << "timestamp_iso,impl_name,scenario,N,seed,rep_id,ops_in_run,total_time_ns,ns_per_op,init_time_ns_if_recorded,relocations_count,conversions_count\n";
}

void write_csv_row(std::ofstream& file, const Result& res) {
    file << res.timestamp_iso << ","
         << res.impl_name << ","
         << res.scenario << ","
         << res.N << ","
         << res.seed << ","
         << res.rep_id << ","
         << res.ops_in_run << ","
         << res.total_time_ns << ","
         << std::fixed << std::setprecision(4) << res.ns_per_op << ","
         << res.init_time_ns << ","
         << res.counters.relocations << ","
         << res.counters.conversions << "\n";
}

using TimePoint = std::chrono::high_resolution_clock::time_point;
TimePoint time_now() { return std::chrono::high_resolution_clock::now(); }
long long duration_ns(TimePoint start, TimePoint end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

void run_scenario(IInitializableArray& array, const Config& config, Result& result) {
    std::mt19937 rng(config.seed);
    std::uniform_int_distribution<size_t> index_dist(0, config.N - 1);
    std::uniform_int_distribution<long long> value_dist(-1000, 1000);
    
    array.reset_counters();
    
    if (config.scenario == "INIT_ONLY") {
        auto start = time_now(); array.init(42); auto end = time_now();
        result.ops_in_run = 1; result.total_time_ns = duration_ns(start, end);
        result.init_time_ns = result.total_time_ns;
    } else if (config.scenario == "READ_UNWRITTEN") {
        array.init(123);
        size_t M = std::min((size_t)1e6, 10 * config.N);
        std::vector<size_t> indices(M); for(size_t i=0;i<M;++i) indices[i] = index_dist(rng);
        auto start = time_now();
        volatile long long sink=0;
        for (size_t i = 0; i < M; ++i) { sink ^= array.read(indices[i]); }
        auto end = time_now();
        result.ops_in_run=M; result.total_time_ns=duration_ns(start,end);
    } else if (config.scenario == "WRITE_SEQUENTIAL") {
        array.init(0);
        auto start = time_now();
        for (size_t i = 0; i < config.N; ++i) array.write(i, i);
        auto end = time_now();
        result.ops_in_run=config.N; result.total_time_ns=duration_ns(start,end);
    } else if (config.scenario == "WRITE_RANDOM") {
        array.init(0);
        size_t M = std::min((size_t)1e6, config.N);
        std::vector<size_t> indices(M); for(size_t i=0;i<M;++i) indices[i] = index_dist(rng);
        auto start = time_now();
        for (size_t i = 0; i < M; ++i) array.write(indices[i], value_dist(rng));
        auto end = time_now();
        result.ops_in_run=M; result.total_time_ns=duration_ns(start,end);
    } else if (config.scenario.rfind("MIXED_",0)==0) {
        std::string perc = config.scenario.substr(6);
        size_t rpos = perc.find('R'), wpos = perc.find('W');
        int read_pct = std::stoi(perc.substr(rpos+1, wpos-rpos-1));
        array.init(42);
        size_t M = std::min((size_t)1e6, config.N);
        std::vector<size_t> indices(M);
        std::vector<uint8_t> ops(M);
        for(size_t i=0;i<M;++i){ indices[i]=index_dist(rng); ops[i]=(rng()%100<read_pct?0u:1u); }
        auto start = time_now();
        volatile long long sink=0;
        for (size_t i = 0; i < M; ++i) {
            if (ops[i]==0) sink ^= array.read(indices[i]);
            else array.write(indices[i], value_dist(rng));
        }
        auto end = time_now();
        result.ops_in_run=M; result.total_time_ns=duration_ns(start,end);
    } else if (config.scenario == "ADVERSARIAL_HOTSPOT") {
        array.init(0);
        size_t M = std::min((size_t)1e6, config.N);
        size_t hotspot_size = std::max((size_t)1, config.N/10);
        auto start = time_now();
        for (size_t i=0;i<M;++i){
            size_t idx = (rng()%2==0) ? (rng()%hotspot_size) : index_dist(rng);
            array.write(idx, value_dist(rng));
        }
        auto end = time_now();
        result.ops_in_run=M; result.total_time_ns=duration_ns(start,end);
    } else {
        throw std::invalid_argument("Unknown scenario: " + config.scenario);
    }
    if (result.ops_in_run>0) result.ns_per_op = double(result.total_time_ns)/result.ops_in_run;
    result.counters = array.get_counters();
}

void run_scenario_direct_stdvector(const Config& config, Result& result) {
    std::mt19937 rng(config.seed);
    std::uniform_int_distribution<size_t> index_dist(0, config.N - 1);
    std::uniform_int_distribution<long long> value_dist(-1000, 1000);
    std::vector<long long> A(config.N, 0);

    auto time_now = [](){ return std::chrono::high_resolution_clock::now(); };
    auto dur_ns = [](auto s, auto e){return std::chrono::duration_cast<std::chrono::nanoseconds>(e-s).count();};

    if (config.scenario == "INIT_ONLY") {
        auto s=time_now(); std::fill(A.begin(), A.end(), 42); auto e=time_now();
        result.ops_in_run=1; result.total_time_ns=dur_ns(s,e); result.init_time_ns=result.total_time_ns;
    } else if (config.scenario == "READ_UNWRITTEN") {
        std::fill(A.begin(), A.end(), 123);
        size_t M = std::min((size_t)1e6, 10*config.N);
        std::vector<size_t> idx(M); for(size_t i=0;i<M;++i) idx[i]=index_dist(rng);
        auto s=time_now(); volatile long long sink=0; for(size_t i=0;i<M;++i) sink ^= A[idx[i]]; auto e=time_now();
        result.ops_in_run=M; result.total_time_ns=dur_ns(s,e);
    } else if (config.scenario == "WRITE_SEQUENTIAL") {
        std::fill(A.begin(), A.end(), 0);
        auto s=time_now(); for (size_t i=0;i<config.N;++i) A[i]=i; auto e=time_now();
        result.ops_in_run=config.N; result.total_time_ns=dur_ns(s,e);
    } else if (config.scenario == "WRITE_RANDOM") {
        std::fill(A.begin(), A.end(), 0);
        size_t M = std::min((size_t)1e6, config.N);
        std::vector<size_t> idx(M); for(size_t i=0;i<M;++i) idx[i]=index_dist(rng);
        auto s=time_now(); for(size_t i=0;i<M;++i) A[idx[i]] = value_dist(rng); auto e=time_now();
        result.ops_in_run=M; result.total_time_ns=dur_ns(s,e);
    } else if (config.scenario.rfind("MIXED_",0)==0) {
        std::fill(A.begin(), A.end(), 42);
        std::string perc = config.scenario.substr(6);
        size_t rpos=perc.find('R'), wpos=perc.find('W');
        int read_pct=std::stoi(perc.substr(rpos+1, wpos-rpos-1));
        size_t M = std::min((size_t)1e6, config.N);
        std::vector<size_t> idx(M); for(size_t i=0;i<M;++i) idx[i]=index_dist(rng);
        std::vector<uint8_t> ops(M); for(size_t i=0;i<M;++i) ops[i]=(rng()%100<read_pct?0u:1u);
        auto s=time_now(); volatile long long sink=0; 
        for(size_t i=0;i<M;++i){ if(ops[i]==0) sink ^= A[idx[i]]; else A[idx[i]] = value_dist(rng); } 
        auto e=time_now();
        result.ops_in_run=M; result.total_time_ns=dur_ns(s,e);
    } else if (config.scenario == "ADVERSARIAL_HOTSPOT") {
        std::fill(A.begin(), A.end(), 0);
        size_t M = std::min((size_t)1e6, config.N);
        size_t hotspot = std::max((size_t)1, config.N/10);
        auto s=time_now();
        for(size_t i=0;i<M;++i){ size_t j = (rng()%2==0) ? (rng()%hotspot) : index_dist(rng); A[j]=value_dist(rng); }
        auto e=time_now();
        result.ops_in_run=M; result.total_time_ns=dur_ns(s,e);
    } else {
        throw std::invalid_argument("Unknown scenario: " + config.scenario);
    }
    if (result.ops_in_run>0) result.ns_per_op = double(result.total_time_ns)/result.ops_in_run;
    result.counters = {};
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--verify") {
        if (argc < 3) { print_usage(); return 1; }
        std::string impl_to_verify = argv[2];
        size_t N = (argc > 3) ? std::stoull(argv[3]) : 10000ULL;
        unsigned int seed = (argc > 4) ? std::stoul(argv[4]) : 42U;
        try { verify_correctness(impl_to_verify, N, seed); } catch(const std::bad_alloc&){ std::cerr<<"Out of memory at N="<<N<<"\n"; }
        return 0;
    }

    std::vector<std::string> impl_names = {"std_vector", "sec3", "sec4", "std_vector_direct"};
    std::vector<std::string> scenarios = {
        "INIT_ONLY","READ_UNWRITTEN","WRITE_SEQUENTIAL","WRITE_RANDOM",
        "MIXED_R90W10","MIXED_R80W20","MIXED_R70W30","MIXED_R50W50","MIXED_R30W70","MIXED_R10W90",
        "ADVERSARIAL_HOTSPOT"
    };
    std::vector<size_t> N_list = {10000ULL, 100000ULL, 1000000ULL};
    unsigned int seed = 42U;
    int reps = 3;
    std::string outfile = "results.csv";

    for(int i=1;i<argc;++i){
        std::string a=argv[i];
        if(a=="--Ns" && i+1<argc){ N_list = parse_sizes(argv[++i]); }
        else if(a=="--reps" && i+1<argc){ reps = std::stoi(argv[++i]); }
        else if(a=="--seed" && i+1<argc){ seed = std::stoul(argv[++i]); }
        else if(a=="--impls" && i+1<argc){
            impl_names.clear();
            std::stringstream ss(argv[++i]); std::string tok;
            while(std::getline(ss,tok,',')) if(!tok.empty()) impl_names.push_back(tok);
        } else if(a=="--outfile" && i+1<argc){ outfile = argv[++i]; }
        else if(a=="--help" || a=="-h"){ print_usage(); return 0; }
    }

    std::ofstream csv_file(outfile);
    if (!csv_file.is_open()) { std::cerr<<"Error: cannot open "<<outfile<<"\n"; return 1; }
    write_csv_header(csv_file);

    for(const auto& impl_name : impl_names){
        for(const auto& N : N_list){
            if ((impl_name == "sec3" && N % 2 != 0) || (impl_name == "sec4" && N % 4 != 0)) continue;
            for(const auto& scenario : scenarios){
                for(int rep=1; rep<=reps; ++rep){
                    Config config{impl_name, scenario, N, seed, rep};
                    std::cout<<"Running: "<<impl_name<<" "<<scenario<<" N="<<N<<" seed="<<seed<<" rep="<<rep<<"...\n";
                    try {
                        Result result;
                        result.timestamp_iso = get_current_timestamp();
                        result.impl_name = impl_name; result.scenario = scenario; result.N=N; result.seed=seed; result.rep_id=rep;
                        if(impl_name=="std_vector_direct"){
                            run_scenario_direct_stdvector(config, result);
                        }else{
                            std::unique_ptr<IInitializableArray> array_impl;
                            if (impl_name=="std_vector") array_impl = std::make_unique<StdVectorWrapper>(N);
                            else if (impl_name=="sec3") array_impl = std::make_unique<InPlaceArraySec3>(N);
                            else if (impl_name=="sec4") array_impl = std::make_unique<InPlaceArraySec4>(N);
                            if (!array_impl) continue;
                            run_scenario(*array_impl, config, result);
                        }
                        write_csv_row(csv_file, result);
                        csv_file.flush();
                    } catch(const std::bad_alloc&){
                        std::cerr<<"Out of memory at N="<<N<<". Skipping.\n";
                    } catch(const std::exception& e){
                        std::cerr<<" ERROR: "<<e.what()<<"\n";
                    }
                }
            }
        }
    }

    std::cout<<"\nExperiment suite finished. Results saved to "<<outfile<<"\n";
    std::cout<<"To run the correctness checker: ./benchmark --verify <sec3|sec4> [N] [seed]\n";
    return 0;
}
