#ifndef UTILS_HPP
#define UTILS_HPP

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

using std::string;

using hash_t     = uint64_t;
using pos_t      = int32_t;
using kmer_num_t = int32_t;

const double EPS = 1e-7;

class Timer {
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point_, end_time_point_;
    double accumulated_time_;
    bool running_;

public:
    Timer() : accumulated_time_(0.0), running_(false) {}

    void start() {
        if (!running_) {
            start_time_point_ = std::chrono::high_resolution_clock::now();
            running_ = true;
        }
    }

    void stop() {
		assert(running_);
        if (running_) {
            end_time_point_ = std::chrono::high_resolution_clock::now();
            accumulated_time_ += std::chrono::duration<double>(end_time_point_ - start_time_point_).count();
            running_ = false;
        }
    }

    double secs() const {
		assert(!running_);
		return accumulated_time_;
    }
};

class Timers {
private:
    std::unordered_map<std::string, Timer> timers_;

public:
    void start(const std::string& name) {
        timers_[name].start();
    }

    void stop(const std::string& name) {
        auto it = timers_.find(name);
        if (it != timers_.end()) {
            it->second.stop();
        }
    }

    double secs(const std::string& name) const {
        auto it = timers_.find(name);
		assert(it != timers_.end());
        if (it != timers_.end()) {
            return it->second.secs();
        }
        return 0.0;
    }

    double perc(const std::string& name, const std::string& total) const {
        auto it = timers_.find(name);
		assert(it != timers_.end());
        auto it_total = timers_.find(total);
		assert(it_total != timers_.end());
        if (it != timers_.end()) {
			if (it_total != timers_.end()) {
				return it->second.secs() / it_total->second.secs() * 100.0;
			}
        }
        return 0.0;
    }
};

class Counter {
private:
    int count_;

public:
    Counter() : count_(0) {}
    void inc(int value = 1) { count_ += value; }
    int count() const { return count_; }
};

class Counters {
private:
    std::unordered_map<std::string, Counter> counters_;

public:
    void inc(const std::string& name, int value = 1) {
		auto it = counters_.find(name);
        if (it == counters_.end()) {
            counters_[name] = Counter();
			it = counters_.find(name);
        }
        counters_[name].inc(value);
    }

    int count(const std::string& name) const {
        assert(counters_.find(name) != counters_.end());
        return counters_.at(name).count();
    }

    double frac(const std::string& name, const std::string& total) const {
        assert(counters_.find(name) != counters_.end());
        assert(counters_.find(total) != counters_.end());
        return double(counters_.at(name).count()) / double(counters_.at(total).count());
	}

    double perc(const std::string& name, const std::string& total) const {
		return 100.0 * frac(name, total);
	}
};

struct Match {
	kmer_num_t kmer_ord;
	pos_t P_l;
	pos_t T_r;
	pos_t t_pos;
	//bool strand;  // 0 - forward, 1 - reverse

	//char get_strand() const {
	//	return strand ? '-' : '+';
	//}
};

struct Mapping {
	int k; 	   // kmer size
	pos_t P_sz;     // pattern size |P| bp 
	pos_t p_sz;     // pattern sketch size |p| kmers
	int matches;  // L.size() -- total number of matches in `t' 
	pos_t T_l;      // the position of the leftmost nucleotide of the mapping
	pos_t T_r;      // the position of the rightmost nucleotide of the mapping
	int xmin;     // the number of kmers in the intersection between the pattern and its mapping in `t'
	pos_t dT_l;      // delta to be applied before output
	pos_t dT_r;      // -- || --
	double J;          // Jaccard score/similarity [0;1]
	double map_time;

	Mapping(int k=0, pos_t P_sz=0, pos_t p_sz=0, int matches=0, pos_t T_l=0, pos_t T_r=0, int xmin=0, pos_t dT_l=0, pos_t dT_r=0, double J=0.0)
		: k(k), P_sz(P_sz), p_sz(p_sz), matches(matches), T_l(T_l), T_r(T_r), xmin(xmin), dT_l(dT_l), dT_r(dT_r), J(J) {}
};

template<typename TT> auto prev(const typename TT::iterator &it) {
    auto pr = it; return --pr;
}

#endif