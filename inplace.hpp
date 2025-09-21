// Section 3 (block=2, 2ℓ extra bits) and Section 4 (block=4, 1 extra bit).
//   g++ -O3 -std=c++17 -DNDEBUG main.cpp -o benchmark
//   ./benchmark
#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <iostream>
#include <functional>
#include <algorithm>

struct Counters {
    std::size_t reads{0};
    std::size_t writes{0};
    std::size_t inits{0};
    std::size_t relocations{0};
    std::size_t conversions{0};
};

struct IInitializableArray {
    virtual ~IInitializableArray() = default;

    virtual void init(long long v) = 0;
    virtual long long read(std::size_t i) = 0;
    virtual void write(std::size_t i, long long v) = 0;

    virtual const char* name() const = 0;

    virtual void reset_counters() = 0;
    virtual Counters get_counters() const = 0;

    virtual void enable_verification() = 0;
    virtual bool verify_correctness() = 0;
    virtual void dump_state_on_failure(std::size_t focus_index) = 0;
};

class StdVectorWrapper : public IInitializableArray {
public:
    explicit StdVectorWrapper(std::size_t n) : N(n), data(n, 0) {}

    const char* name() const override { static std::string s = "std_vector"; return s.c_str(); }

    void init(long long v) override {
        ++ctr.inits;
        std::fill(data.begin(), data.end(), v);
        initv_shadow = v;
        ++epoch;
    }
    long long read(std::size_t i) override { ++ctr.reads; bounds(i); return data[i]; }
    void write(std::size_t i, long long v) override { ++ctr.writes; bounds(i); data[i] = v; }

    void reset_counters() override { ctr = Counters{}; }
    Counters get_counters() const override { return ctr; }

    void enable_verification() override { verifying = true; }
    bool verify_correctness() override { return true; }
    void dump_state_on_failure(std::size_t focus_index) override {
        std::cerr << "[StdVectorWrapper] N="<<N<<" focus="<<focus_index
                  <<" value="<<(focus_index<N?data[focus_index]:0)<<"\n";
    }
private:
    void bounds(std::size_t i) const { if (i>=N) throw std::out_of_range("index"); }
    std::size_t N;
    std::vector<long long> data;
    Counters ctr;
    bool verifying{false};
    long long initv_shadow{0};
    std::size_t epoch{0};
};

class VerifiableBase : public IInitializableArray {
public:
    explicit VerifiableBase(std::size_t n): N(n) {}
    void enable_verification() override {
        verifying = true;
        shadow.assign(N, 0);
        stamp.assign(N, 0);
        shadow_initv = 0;
        shadow_epoch = 1;
    }
protected:
    void shadow_on_init(long long v){
        if(!verifying) return;
        shadow_initv = v;
        ++shadow_epoch;
        if(shadow_epoch==0){ std::fill(stamp.begin(), stamp.end(), 0); shadow_epoch=1; }
    }
    void shadow_on_write(std::size_t i, long long v){
        if(!verifying) return;
        shadow[i]=v; stamp[i]=shadow_epoch;
    }
    bool shadow_check_against(std::function<long long(std::size_t)> read_actual){
        if(!verifying) return true;
        for(std::size_t i=0;i<N;++i){
            long long expect = (stamp[i]==shadow_epoch)?shadow[i]:shadow_initv;
            long long got = read_actual(i);
            if(expect!=got){
                std::cerr<<"[Verifier] mismatch at i="<<i<<" expect="<<expect<<" got="<<got<<"\n";
                return false;
            }
        }
        return true;
    }
    std::size_t N;
    bool verifying{false};
private:
    std::vector<long long> shadow;
    std::vector<std::uint32_t> stamp;
    long long shadow_initv{0};
    std::uint32_t shadow_epoch{0};
};

// ---------------- Section 3 (block=2) ----------------
class InPlaceArraySec3 : public VerifiableBase {
public:
    explicit InPlaceArraySec3(std::size_t n)
        : VerifiableBase(n), N_blocks(n/2), A(n,0) {
        if(n==0) throw std::invalid_argument("N>0 required");
        if(n%2!=0) throw std::invalid_argument("Section 3 requires even N");
        initv=0; b=0;
    }
    const char* name() const override { static std::string s="sec3"; return s.c_str(); }

    void init(long long v) override {
        ++ctr.inits; initv=v; b=0; shadow_on_init(v);
    }
    long long read(std::size_t i) override { ++ctr.reads; if(i>=N) throw std::out_of_range("index"); return read_impl(i); }
    void write(std::size_t i, long long v) override {
        ++ctr.writes; if(i>=N) throw std::out_of_range("index");
        write_impl(i,v); if(verifying) shadow_on_write(i,v);
    }

    void reset_counters() override { ctr = Counters{}; }
    Counters get_counters() const override { return ctr; }

    bool verify_correctness() override {
        if(!shadow_check_against([this](std::size_t j){return this->read_impl(j);}))
            return false;
        for(std::size_t i=0;i<N_blocks;++i){
            long long k=chainedTo_block(i);
            if(k>=0){
                std::size_t j=static_cast<std::size_t>(k);
                if(A[first_of(j)] != static_cast<long long>(first_of(i))){
                    std::cerr<<"[Invariant] chain asymmetry B"<<i<<" <-> B"<<j<<"\n"; return false;
                }
            }
        }
        return true;
    }
    void dump_state_on_failure(std::size_t focus_index) override {
        std::cerr<<"[Sec3 dump] N="<<N<<" blocks="<<N_blocks<<" b="<<b
                 <<" initv="<<initv<<" focus="<<focus_index<<"\n";
        std::size_t i0 = (focus_index/2);
        std::size_t start = (i0>4? i0-4:0);
        std::size_t end = std::min(N_blocks, i0+5);
        for(std::size_t bi=start; bi<end; ++bi){
            std::cerr<<"  B"<<bi<<(bi<b?" [UCA]":" [WCA]")
                     <<" : ("<<A[2*bi]<<","<<A[2*bi+1]<<")\n";
        }
    }

private:
    inline std::size_t block_of(std::size_t i) const { return i>>1; }
    inline std::size_t first_of(std::size_t blk) const { return (blk<<1); }

    long long chainedTo_block(std::size_t bi) const {
        long long k0 = A[first_of(bi)];
        if((k0 & 1LL)!=0) return -1;
        if(k0<0 || static_cast<std::size_t>(k0)>=N) return -1;
        std::size_t k = static_cast<std::size_t>(k0)>>1;
        bool cross = ((bi<b && k>=b) || (k<b && bi>=b));
        if(!cross) return -1;
        if(A[static_cast<std::size_t>(k0)] != static_cast<long long>(first_of(bi))) return -1;
        return static_cast<long long>(k);
    }
    void makeChain(std::size_t bi, std::size_t bj){
        A[first_of(bi)] = static_cast<long long>(first_of(bj));
        A[first_of(bj)] = static_cast<long long>(first_of(bi));
        ++ctr.conversions;
    }
    void breakChain(std::size_t bi){
        long long k = chainedTo_block(bi);
        if(k>=0){ std::size_t bj=static_cast<std::size_t>(k); A[first_of(bj)] = static_cast<long long>(first_of(bj)); ++ctr.conversions; }
    }
    void initBlock(std::size_t bi){
        A[first_of(bi)] = initv;
        A[first_of(bi)+1] = initv;
    }
    std::size_t extend(){
        std::size_t s=b;
        long long k=chainedTo_block(s);
        ++b;
        if(k<0){
            initBlock(s);
            breakChain(s);
            return s;
        }else{
            std::size_t bk=static_cast<std::size_t>(k);
            A[first_of(s)] = A[first_of(bk)+1];
            breakChain(s);
            initBlock(bk);
            breakChain(bk);
            ++ctr.relocations;
            return bk;
        }
    }

    long long read_impl(std::size_t i) const {
        std::size_t bi=block_of(i);
        long long k=chainedTo_block(bi);
        if(i < 2*b){
            if(k>=0) return initv;
            return A[i];
        }else{
            if(k>=0){
                std::size_t bk=static_cast<std::size_t>(k);
                return ( (i&1U)==0 ? A[first_of(bk)+1] : A[i] );
            }else{
                return initv;
            }
        }
    }
    void write_impl(std::size_t i, long long v){
        std::size_t bi=block_of(i);
        long long k=chainedTo_block(bi);

        if(bi<b){
            if(k<0){
                A[i]=v;
                breakChain(bi);
            }else{
                std::size_t bj=extend();
                if(bj==bi){ A[i]=v; breakChain(bi); }
                else{
                    std::swap(A[first_of(bj)], A[first_of(bi)]);
                    std::swap(A[first_of(bj)+1], A[first_of(bi)+1]);
                    ++ctr.relocations;
                    makeChain(bj, static_cast<std::size_t>(k));
                    initBlock(bi);
                    A[i]=v;
                    breakChain(bi);
                }
            }
        }else{
            if(k>=0){
                std::size_t bk=static_cast<std::size_t>(k);
                if((i&1U)==0) A[first_of(bk)+1]=v; else A[i]=v;
            }else{
                std::size_t bk2=extend();
                if(bk2==bi){ A[i]=v; breakChain(bi); }
                else{
                    initBlock(bi);
                    makeChain(bk2, bi);
                    if((i&1U)==0) A[first_of(bk2)+1]=v; else A[i]=v;
                }
            }
        }
    }

    std::size_t N_blocks;
    std::vector<long long> A;
    std::size_t b{0};
    long long initv{0};
    Counters ctr;
};

// ---------------- Section 4 (block=4) ----------------
class InPlaceArraySec4 : public VerifiableBase {
public:
    explicit InPlaceArraySec4(std::size_t n)
        : VerifiableBase(n), N_blocks(n/4), A(n,0) {
        if(n==0) throw std::invalid_argument("N>0 required");
        if(n%4!=0) throw std::invalid_argument("Section 4 requires N%4==0");
        initv=0; b=0; flag=false;
    }
    const char* name() const override { static std::string s="sec4"; return s.c_str(); }

    void init(long long v) override {
        ++ctr.inits; initv=v; b=0;
        flag = (N_blocks==0); 
        sync_meta_to_A();     
        shadow_on_init(v);
    }
    long long read(std::size_t i) override { ++ctr.reads; if(i>=N) throw std::out_of_range("index"); return read_impl(i); }
    void write(std::size_t i, long long v) override {
        ++ctr.writes; if(i>=N) throw std::out_of_range("index");
        write_impl(i,v); if(verifying) shadow_on_write(i,v);
    }

    void reset_counters() override { ctr = Counters{}; }
    Counters get_counters() const override { return ctr; }

    bool verify_correctness() override {
        if(!shadow_check_against([this](std::size_t j){return this->read_impl(j);}))
            return false;
        for(std::size_t i=0;i<N_blocks;++i){
            long long k=chainedTo_block(i);
            if(k>=0){
                std::size_t j=static_cast<std::size_t>(k);
                if(A[first_of(j)] != static_cast<long long>(first_of(i))){
                    std::cerr<<"[Invariant] chain asymmetry B"<<i<<" <-> B"<<j<<"\n"; return false;
                }
            }
        }
        return true;
    }
    void dump_state_on_failure(std::size_t focus_index) override {
        std::cerr<<"[Sec4 dump] N="<<N<<" blocks="<<N_blocks<<" b="<<b
                 <<" initv="<<initv<<" flag="<<(flag?1:0)<<" focus="<<focus_index<<"\n";
        std::size_t bi=block_of(focus_index);
        std::size_t start=(bi>3?bi-3:0), end=std::min(N_blocks, bi+4);
        for(std::size_t j=start;j<end;++j){
            std::cerr<<"  B"<<j<<(j<b?" [UCA]":" [WCA]")<<" : ("
                     <<A[first_of(j)]<<","<<A[first_of(j)+1]<<","
                     <<A[first_of(j)+2]<<","<<A[first_of(j)+3]<<")\n";
        }
    }
private:
    inline std::size_t block_of(std::size_t i) const { return i>>2; }
    inline std::size_t first_of(std::size_t blk) const { return (blk<<2); }
    void sync_flag(){ flag = (b>=N_blocks); }
    void sync_meta_to_A(){
        sync_flag();
        if(!flag){
            std::size_t mb = N_blocks-1;
            A[first_of(mb)+1] = initv;
            A[first_of(mb)+2] = static_cast<long long>(b);
        }
    }
    long long chainedTo_block(std::size_t bi) const {
        long long k0=A[first_of(bi)];
        if((k0 & 3LL)!=0) return -1;
        if(k0<0 || static_cast<std::size_t>(k0)>=N) return -1;
        std::size_t k = static_cast<std::size_t>(k0)>>2;
        bool cross = ((bi<b && k>=b) || (k<b && bi>=b));
        if(!cross) return -1;
        if(A[static_cast<std::size_t>(k0)] != static_cast<long long>(first_of(bi))) return -1;
        return static_cast<long long>(k);
    }
    void makeChain(std::size_t bi, std::size_t bj){
        A[first_of(bi)] = static_cast<long long>(first_of(bj));
        A[first_of(bj)] = static_cast<long long>(first_of(bi));
        ++ctr.conversions;
    }
    void breakChain(std::size_t bi){
        long long k=chainedTo_block(bi);
        if(k>=0){ std::size_t bj=static_cast<std::size_t>(k); A[first_of(bj)] = static_cast<long long>(first_of(bj)); ++ctr.conversions; }
    }
    void initBlock(std::size_t bi){
        A[first_of(bi)] = initv;
        A[first_of(bi)+1] = initv;
        A[first_of(bi)+2] = initv;
        A[first_of(bi)+3] = initv;
    }
    std::size_t extend(){
        std::size_t s=b;
        long long k=chainedTo_block(s);
        ++b;
        if(k<0){
            initBlock(s);
            breakChain(s);
            sync_meta_to_A();      
            return s;
        }else{
            std::size_t bk=static_cast<std::size_t>(k);
            A[first_of(s)    ] = A[first_of(bk)+1];
            A[first_of(s) + 1] = A[first_of(bk)+2];
            A[first_of(s) + 2] = A[first_of(bk)+3];
            breakChain(s);
            initBlock(bk);
            breakChain(bk);
            ++ctr.relocations;
            sync_meta_to_A();      
            return bk;
        }
    }

    long long read_impl(std::size_t i) const {
        if(flag) return A[i]; 
        std::size_t bi=block_of(i);
        long long k=chainedTo_block(bi);
        if(i < 4*b){
            if(k>=0) return initv;
            return A[i];
        }else{
            if(k>=0){
                std::size_t bk=static_cast<std::size_t>(k);
                switch(i & 3U){
                    case 0: return A[first_of(bk)+1];
                    case 1: return A[first_of(bk)+2];
                    case 2: return A[first_of(bk)+3];
                    case 3: return A[i];
                }
            }else{
                return initv;
            }
        }
        return 0;
    }
    void write_impl(std::size_t i, long long v){
        if(flag){ A[i]=v; return; }
        std::size_t bi=block_of(i);
        long long k=chainedTo_block(bi);

        if(bi<b){
            if(k<0){
                A[i]=v;
                breakChain(bi);
            }else{
                std::size_t bj=extend();
                if(bj==bi){ A[i]=v; breakChain(bi); }
                else{
                    for(int t=0;t<4;++t) std::swap(A[first_of(bj)+t], A[first_of(bi)+t]);
                    ++ctr.relocations;
                    makeChain(bj, static_cast<std::size_t>(k));
                    initBlock(bi);
                    A[i]=v;
                    breakChain(bi);
                }
            }
        }else{
            if(k>=0){
                std::size_t bk=static_cast<std::size_t>(k);
                switch(i & 3U){
                    case 0: A[first_of(bk)+1]=v; break;
                    case 1: A[first_of(bk)+2]=v; break;
                    case 2: A[first_of(bk)+3]=v; break;
                    case 3: A[i]=v; break;
                }
            }else{
                std::size_t bk2=extend();
                if(bk2==bi){ A[i]=v; breakChain(bi); }
                else{
                    initBlock(bi);
                    makeChain(bk2, bi);
                    switch(i & 3U){
                        case 0: A[first_of(bk2)+1]=v; break;
                        case 1: A[first_of(bk2)+2]=v; break;
                        case 2: A[first_of(bk2)+3]=v; break;
                        case 3: A[i]=v; break;
                    }
                }
            }
        }
    }

    std::size_t N_blocks;
    std::vector<long long> A;
    std::size_t b{0};
    long long initv{0};
    bool flag{false};
    Counters ctr;
};
