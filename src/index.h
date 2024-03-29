#pragma once

#include <iostream>
#include <string>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "sketch.h"
#include "utils.h"
#include "io.h"

namespace sweepmap {

class SketchIndex;

// Hit -- a kmer hit in the reference T
struct Hit {  // TODO: compress into a 32bit field
	pos_t r;            // right end of the kmer [l, r), where l+k=r
	pos_t tpos;		    // position in the reference sketch
	bool strand;
	segm_t segm_id;
	Hit() {}
	Hit(const Kmer &kmer, pos_t tpos, segm_t segm_id)
		: r(kmer.r), tpos(tpos), strand(kmer.strand), segm_id(segm_id) {}
};

// Seed -- a kmer with metadata (a position in the queyr P and number of hits in the reference T)
struct Seed {
	Kmer kmer;
	int r_first, r_last;
	int hits_in_T;
	Seed(const Kmer &kmer, pos_t r_first, pos_t r_last, int hits_in_T) :
		kmer(kmer), r_first(r_first), r_last(r_last), hits_in_T(hits_in_T) {}	
};

// Match -- a pair of a seed and a hit
struct Match {
	Seed seed;
	Hit hit;
	int seed_num; // seed number among the chosen seeds, used for indexing the histogram
	Match(const Seed &seed, const Hit &hit, int seed_num)
		: seed(seed), hit(hit), seed_num(seed_num) {}
	
	inline bool is_same_strand() const {
		return seed.kmer.strand == hit.strand;
	}
};

struct RefSegment {
	Sketch::sketch_t kmers;
	std::string name;
	std::string seq;   // empty if only mapping and no alignment
	int sz;
	RefSegment(const Sketch &sk, const std::string &name, const std::string &seq, const int sz)
		: kmers(sk.kmers), name(name), seq(seq), sz(sz) {}
};

class SketchIndex {

public:
	std::vector<RefSegment> T;
	const params_t &params;
	ankerl::unordered_dense::map<hash_t, Hit> h2single;               // all sketched kmers with =1 hit
	ankerl::unordered_dense::map<hash_t, std::vector<Hit>> h2multi;   // all sketched kmers with >1 hits
	Timers *timer;
	Counters *C;

	void get_kmer_stats() {
		std::vector<int> hist(10, 0);
		int max_occ = 0;
        C->inc("indexed_hits", h2single.size());
        C->inc("indexed_kmers", h2single.size());
		for (const auto& h2p : h2multi) {
			int occ = h2p.second.size();
			C->inc("indexed_hits", occ);
			C->inc("indexed_kmers");
			if (occ >= (int)hist.size()-1) {
				hist.back() += occ;
				if (occ > max_occ)
					max_occ = occ;
			} else 
				hist[occ] += occ;
		}
		C->inc("indexed_highest_freq_kmer", max_occ);
	}

	int count(hash_t h) const {
		if (h2single.contains(h)) return 1;
		else if (h2multi.contains(h)) return h2multi.at(h).size();
		else return 0;
	}

	void add_matches(std::vector<Match> *matches, const Seed &s, int seed_num) const {
		if (s.hits_in_T == 1) {
			assert(h2single.contains(s.kmer.h));
			matches->push_back(Match(s, h2single.at(s.kmer.h), seed_num));
				
		} else {
			assert(s.hits_in_T > 1);
			assert(h2multi.contains(s.kmer.h));
			for (const auto &hit: h2multi.at(s.kmer.h))
				matches->push_back(Match(s, hit, seed_num));
		}	
	}

	void erase_frequent_kmers() {
		std::vector<hash_t> blacklisted_h;
		for (const auto &[h, hits]: h2multi)
			if (hits.size() > (size_t)params.max_matches) {
				blacklisted_h.push_back(h);
				C->inc("blacklisted_kmers");
				C->inc("blacklisted_hits", hits.size());
			}

		for (auto h: blacklisted_h)
			h2multi.erase(h);
	}

	void populate_h2pos(const Sketch& sketch, int segm_id) {
		// TODO: skip creating the sketch structure
		for (size_t tpos = 0; tpos < sketch.kmers.size(); ++tpos) {
			const Kmer& kmer = sketch.kmers[tpos];
			const auto hit = Hit(kmer, tpos, segm_id);
			if (!h2single.contains(kmer.h))
				h2single[kmer.h] = hit; 
            else if (h2multi[kmer.h].size() < (size_t)params.max_matches + 1)
                h2multi[kmer.h].push_back(hit);
		}
	}

	void add_segment(const kseq_t *seq, const Sketch& sketch) {
		string segm_name = seq->name.s;
		string segm_seq = seq->seq.s;
		int segm_size = seq->seq.l;
		T.push_back(RefSegment(sketch, segm_name, segm_seq, segm_size));
		C->inc("segments");
		C->inc("total_nucls", segm_size);
		populate_h2pos(sketch, T.size()-1);
	}

	SketchIndex(const params_t &params, Timers *timer, Counters *C)
		: params(params), timer(timer), C(C) {}

	void build_index(const std::string &tFile) {
		timer->start("indexing");
		cerr << "Indexing " << params.tFile << "..." << endl;
		timer->start("index_reading");
		read_fasta_klib(params.tFile, [this](kseq_t *seq) {
			timer->stop("index_reading");
			timer->start("index_sketching");
			Sketch t(seq->seq.s);
			timer->stop("index_sketching");

			timer->start("index_initializing");
			add_segment(seq, t);
			timer->stop("index_initializing");

			timer->start("index_reading");
		});

		// if a kmer is present in both single and multi, we move it out of single to multi
		for (auto &[h, hits] : h2multi) {
			if (h2single.contains(h)) {
				hits.push_back(h2single.at(h));
				h2single.erase(h);
			}
		}
		timer->stop("index_reading");
		timer->stop("indexing");

		get_kmer_stats();
        C->inc("blacklisted_kmers", 0);
        C->inc("blacklisted_hits", 0);
		erase_frequent_kmers();
		print_stats();
	}

	void print_stats() {
		cerr << std::fixed << std::setprecision(1);
		cerr << "Index stats:" << endl;
        printMemoryUsage();
		cerr << " | total nucleotides:     " << C->count("total_nucls") << endl;
		cerr << " | index segments:        " << C->count("segments") << " (~" << 1.0*C->count("total_nucls") / C->count("segments") << " nb per segment)" << endl;
		cerr << " | indexed kmers:         " << C->count("indexed_kmers") << endl;
		cerr << " | indexed hits:          " << C->count("indexed_hits") << " ("
												<< double(params.k)*C->perc("indexed_hits", "total_nucls") << "\% of the index, "
												<< "~" << C->frac("indexed_hits", "indexed_kmers") << " per kmer)" << endl;
		cerr << " | | most frequent kmer:      " << C->count("indexed_highest_freq_kmer") << " times." << endl;
		cerr << " | | blacklisted kmers:       " << C->count("blacklisted_kmers") << " (" << C->perc("blacklisted_kmers", "indexed_kmers") << "\%)" << endl;
		cerr << " | | blacklisted hits:        " << C->count("blacklisted_hits") << " (" << C->perc("blacklisted_hits", "indexed_hits") << "\%)" << endl;
	}
};

} // namespace sweepmap