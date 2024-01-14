#include <algorithm>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <deque>
#include <set>

#include "sketch.h"
#include "io.h"

int total_reads(0), total_read_len(0), unmapped_reads(0);
double total_J(0.0);
int total_matches(0), total_mappings(0), total_sketched_kmers(0);

// Return only reasonable matches (i.e. those that are not J-dominated by
// another overlapping match). Runs in O(|all|).
vector<Mapping> filter_reasonable(const params_t &params, const vector<Mapping> &all, const pos_t P_len) {
	vector<Mapping> reasonable;
	deque<Mapping> recent;

	// Minimal separation between mappings to be considered reasonable
	pos_t sep = pos_t((1.0 - params.tThres) * double(P_len));

	// The deque `recent' is sorted decreasingly by J
	//					  _________`recent'_________
	//                   /                          \
	// ---------------- | High J ... Mid J ... Low J | current J
	// already removed    deque.back ... deque.front    to add next
	for (const auto &next: all) {
		// 1. Prepare for adding `curr' by removing from the deque back all
		//    mappings that are too far to the left. This keeps the deque
		//    within |P| from back to front. A mapping can become reasonable
		//    only after getting removed.
		while(!recent.empty() && next.T_l - recent.back().T_l > sep) {
			// If the mapping is not marked as unreasonable (coverted by a preivous better mapping)
			if (recent.back().matches != -1) {
				// Take the leftmost mapping.
				reasonable.push_back(recent.back());
				// Mark the next closeby mappings as not reasonable
				for (auto it=recent.rbegin(); it!=recent.rend() && it->T_l - recent.back().T_l < sep; ++it)
					it->matches = -1;
			}
			// Remove the mapping that is already too much behind.
			recent.pop_back();
		}
		assert(recent.empty() || (recent.back().T_r <= recent.front().T_r && recent.front().T_r <= next.T_r));

		// Now all the mappings in `recent' are close to `curr' 
		// 2. Remove from the deque front all mappings that are strictly
		//    less similar than the current J. This keeps the deque sorted
		//    descending in J from left to right
		while(!recent.empty() && recent.front().J < next.J - EPS)
			recent.pop_front();
		assert(recent.empty() || (recent.back().J >= recent.front().J - EPS && recent.front().J >= next.J - EPS));

		// 3. Add the next mapping to the front
		recent.push_front(next);	 

		// 4. If there is another mapping in the deque, it is near and better.
		if (recent.size() > 1)
			recent.front().matches = -1;
	}

	// 5. Add the last mapping if it is reasonable
	if (!recent.empty() && recent.back().matches != -1)
		reasonable.push_back(recent.back());

	return reasonable;
}

// Extends the mappings to the left and right
void normalize_mappings(const params_t &params, vector<Mapping> *mappings, const pos_t pLen, const string &seqID, const pos_t T_sz) {
	for(auto &m: *mappings) {
		if (params.alignment_edges == alignment_edges_t::extend_equally) {
			int span   = m.T_r - m.T_l + 1;
			//assert(span <= m.P_sz);
			int shift  = (m.P_sz - span) / 2;       assert(shift >= 0);
			m.T_l -= shift; m.T_l = max(0, m.T_l);
			m.T_r += shift; m.T_r = min(T_sz-1, m.T_r);
		} else if (params.alignment_edges == alignment_edges_t::fine) {
			m.T_l += m.dT_l;
			m.T_r += m.dT_r;
		}
	}
}

class SweepMap {
	const SketchIndex *tidx;
	const params_t &params;
	Timers *T;

	// return if the kmer has not been seen before
	inline bool add2hist(
			const hash_t kmer_hash,
			vector<int> *hist,
			unordered_map<hash_t, kmer_num_t> *hash2num,
			kmer_num_t *kmer_num) {
		auto num_it = hash2num->find(kmer_hash);
		if (num_it != hash2num->end()) {
			*kmer_num = num_it->second;
			++(*hist)[*kmer_num];
			return false;
		} else {
			*kmer_num = (kmer_num_t)hist->size();
			hash2num->insert({kmer_hash, *kmer_num});  //(*hash2ord)[kmer_hash] = kmer_ord;
			//ord2hash->push_back(kmer_hash);
			hist->push_back(1);
			//assert(hist->size() == ord2hash->size());
			return true;
		}
	}

	// Returns the Jaccard score of the window [l,r)
	inline double J(size_t p_sz, const vector<Match>::const_iterator l, const vector<Match>::const_iterator r, int xmin) {
		int s_sz = prev(r)->t_pos - l->t_pos + 1;
		if (s_sz < 0) return 0;
		assert(p_sz + s_sz - xmin > 0);
		double scj = double(xmin) / double(p_sz + s_sz - xmin);
		if (!(0.0 <= scj && scj <= 1.0))
			cerr << "ERROR: scj=" << scj << ", xmin=" << xmin << ", p_sz=" << p_sz << ", s_sz=" << s_sz << ", l=" << l->t_pos << ", r=" << r->t_pos << endl;
		//assert (0.0 <= scj && scj <= 1.0);
		return scj;
	}

	// Returns true if the window [l,r) should be extended to the right
	inline bool should_extend_right(
			const vector<int> &hist,
			const vector<Match> &L,
			const vector<Match>::const_iterator l, const vector<Match>::const_iterator r,
			const int xmin, const int Plen_nucl, const int k) {
		if (r == L.end())
			return false;
		bool extension_stays_within_window = r->T_r + k <= l->T_r + Plen_nucl;
		return extension_stays_within_window;
	}

	//void print_matches(const vector<Match> &L) const {
	//	cout << "Matches:" << endl;
	//	for (auto &m: L) {
	//		cout << "kmer_ord=" << m.kmer_ord << ", P_l=" << m.P_l << ", T_r=" << m.T_r << ", t_pos=" << m.t_pos << endl;
	//	}
	//}

	// Initializes the histogram of the pattern and the list of matches
	void match_kmers(const Sketch& p, vector<int> *p_hist, vector<Match> *L) {
		unordered_map<hash_t, kmer_num_t> hash2num;
		L->reserve(P_MULTIPLICITY * p.size());

		vector<kmer_hits_t> match_lists;
		match_lists.reserve(p.size());

		hash_t kmer_hash;
		for(auto p_it = p.begin(); p_it != p.end(); ++p_it) {
			// TODO: limit the number of kmers in the pattern p
			pos_t P_l = p_it->first;
			kmer_hash = p_it->second;
			kmer_num_t kmer_num;

			// Add pattern kmers from the pattern to p_hist 
			if (add2hist(kmer_hash, p_hist, &hash2num, &kmer_num)) {
				auto it = tidx->h2pos.find(kmer_hash);
				if (it != tidx->h2pos.end()) {
					match_lists.push_back(kmer_hits_t(P_l, kmer_num, it->second));
				}
			}
		}

		// Sort the matching kmers by number of hits in the reference
		sort(match_lists.begin(), match_lists.end(), [](const kmer_hits_t &a, const kmer_hits_t &b) { return a.kmers_in_T.size() < b.kmers_in_T.size(); });

		// Get MAX_SEEDS of kmers with the lowest number of hits.
		int total_hits = 0;
		for (int seed=0; seed<(int)match_lists.size(); seed++) {
			if (seed > (int)params.max_seeds) {
				++seeds_limit_reached;
				break;
			}
			kmer_hits_t &res = match_lists[seed];
			for (const auto &hit: res.kmers_in_T) {
				Match m;
				m.P_l      = res.P_l;
				m.kmer_ord = res.kmer_num;
				m.T_r      = hit.first;
				m.t_pos    = hit.second;
				//m->strand  = 0;
				L->push_back(m);
			}
			if ((total_hits += (int)res.kmers_in_T.size()) > (int)params.max_matches) {
				++matches_limit_reached;
				break;
			}
		}

		T->start("sorting");
		// Sort L by ascending positions in reference so that we can next sweep through it.
		sort(L->begin(), L->end(), [](const Match &a, const Match &b) { return a.T_r < b.T_r; });
		T->stop("sorting");
	}


	// vector<int> diff_hist;  // diff_hist[kmer_hash] = #occurences in `p` - #occurences in `s`
	// vector<Match> L;   	   // for all kmers from P in T: <kmer_hash, last_kmer_pos_in_T> * |P| sorted by second
	const vector<Mapping> sweep(vector<int> &diff_hist, const vector<Match> &L, const pos_t p_len, const pos_t P_len) {
		vector<Mapping> mappings;	// List of tripples <i, j, score> of matches
		multiset<pos_t> P_l_set;  // TODO: use a vector instead?

		int xmin = 0;
		Mapping best;
		best.k = params.k;
		best.P_sz = P_len;
		best.p_sz = p_len;

		// Increase the left point end of the window [l,r) one by one. O(matches)
		int i = 0, j = 0;
		for(auto l = L.begin(), r = L.begin(); l != L.end(); ++l, ++i) {
			// Increase the right end of the window [l,r) until it gets out.
			for(; should_extend_right(diff_hist, L, l, r, xmin, P_len, params.k); ++r, ++j) {
				P_l_set.insert(r->P_l);
				// If taking this kmer from T increases the intersection with P.
				if (--diff_hist[r->kmer_ord] >= 0)
					++xmin;
				assert (l->T_r <= r->T_r);
			}

			auto curr_J = J(p_len, l, r, xmin);
			auto m = Mapping(params.k, P_len, p_len, (int)L.size(), l->T_r, prev(r)->T_r, xmin, 0, 0, curr_J);  // TODO: create only if curr_J is high enough

			if (params.alignment_edges == alignment_edges_t::fine) {
				if (P_l_set.size() > 0) {
					m.dT_l = - *P_l_set.begin();
					m.dT_r = P_len - (*P_l_set.rbegin()+params.k);
				}
			}

			if (params.onlybest) {
				if (curr_J > best.J)
					best = m;
			} else {
				if (curr_J > params.tThres)
					mappings.push_back(m);
			}

			P_l_set.erase(P_l_set.find(l->P_l));
			// Prepare for the next step by moving `l` to the right.
			if (++diff_hist[l->kmer_ord] > 0)
				--xmin;

			assert(xmin >= 0);
		}
		assert (xmin == 0);

		if (params.onlybest) { // && best.J > params.tThres)
			mappings.push_back(best);
		}

		return mappings;
	}

	void map(const string &pFile, const std::unordered_map<hash_t, char> &bLstmers) {
		ifstream reads_stream;
		reads_stream.open(pFile);
		if (!reads_stream.is_open()) {
			cerr << "ERROR: Could not open " << params.pFile << endl;
			return;
		}

		//cerr << "aligning reads from " << params.pFile << "..." << endl;
		vector<std::tuple<string, pos_t, Sketch>> pSks;

		T->start("mapping");
		// Load pattern sequences in batches
		while(true) {
			cerr << "Sketching a batch of reads from " << params.pFile << "..." << endl;
			T->start("sketching");
			if (!lMiniPttnSks(reads_stream, params.k, tidx->w, bLstmers, &pSks) && pSks.empty()) {
				T->stop("sketching");
				break;
			}
			T->stop("sketching");

			cerr << "Mapping a batch of reads..." << endl;
			// Iterate over pattern sketches
			for (const auto &[seqID, P_sz, sks] : pSks) {
				total_read_len += P_sz;
				const Sketch p;
				vector<int> p_hist;
				vector<Match> L;

				T->start("matching");
				match_kmers(p, &p_hist, &L);
				T->stop("matching");
				//print_matches(L);

				T->start("sweep");
				//print_sketches(seqID, sks);
				auto mappings = sweep(p_hist, L, (pos_t)p.size(), P_sz);
				T->stop("sweep");

				T->start("postproc");
				auto reasonable_mappings = params.overlaps ? mappings : filter_reasonable(params, mappings, P_sz);
				normalize_mappings(params, &reasonable_mappings, (pos_t)sks.size(), seqID, tidx->T_sz);

				for (auto &m: reasonable_mappings) {
					m.map_time = T->get_secs("sweep") / (double)mappings.size();
					total_J += m.J;
					total_mappings ++;
					total_sketched_kmers += m.p_sz;
					total_matches += m.matches;
				}
				++total_reads;
				if (!reasonable_mappings.empty())
					mappings2paf(params, reasonable_mappings, P_sz, (pos_t)sks.size(), seqID, tidx->T_sz, tidx->name, "");
				else
					++unmapped_reads;

				T->stop("postproc");
			}
			pSks.clear();
		}
		T->stop("mapping");
	}

  public:
	// stats
	int seeds_limit_reached = 0;
	int matches_limit_reached = 0;

	SweepMap(const SketchIndex *tidx, const params_t &params, Timers *T=nullptr)
		: tidx(tidx), params(params), T(T) {
			if (params.tThres < 0.0 || params.tThres > 1.0) {
				cerr << "tThres = " << params.tThres << " outside of [0,1]." << endl;
				exit(1);
			}
		}
};

int main(int argc, char **argv) {
	Timers T;
	T.start("total");

	params_t params;
	if(!prsArgs(argc, argv, &params)) {
		dsHlp();
		return 1;
	}

	unordered_map<hash_t, char> bLstmers;
	if (!params.bLstFl.empty()) {
		bLstmers = readBlstKmers(params.bLstFl);
	}

	T.start("indexing");
	SketchIndex tidx(params.tFile, params.k, params.w, bLstmers);
	T.stop("indexing");

	if (!params.paramsFile.empty()) {
		cerr << "Writing parameters to " << params.paramsFile << "..." << endl;
		auto fout = ofstream(params.paramsFile);
		params.print(fout, false);
	} else {
		params.print(cerr, true);
	}

	SweepMap mapper(&tidx, params, &T);
	mapper.map(params.pFile, bLstmers);
	T.stop("total");

//	print_report(params, T);

	cerr << fixed << setprecision(1);
	// TODO: report average Jaccard similarity
	cerr << "Params:" << endl;
	cerr << " | reference:        " << params.tFile << " (" << tidx.T_sz << " nb)" << endl;
	cerr << " | queries:          " << params.pFile << endl;
	cerr << " | k:                " << params.k << endl;
	cerr << " | w:                " << params.w << endl;
	cerr << " | blacklist file:   " << (params.bLstFl.size() ? params.bLstFl : "-") << endl;
	//cerr << " | hFrac:            " << params.hFrac << endl;
	cerr << " | max_seeds (S):    " << params.max_seeds << endl;
	cerr << " | max_matches (M):  " << params.max_matches << endl;
	cerr << " | onlybest:         " << params.onlybest << endl;
	cerr << " | tThres:           " << params.tThres << endl;
	cerr << "Stats:" << endl;
	cerr << " | Total reads:           " << total_reads << " (~" << 1.0*total_read_len / total_reads << " nb per read)" << endl;
	cerr << " | Sketched read kmers:   " << total_sketched_kmers << " (" << 1.0*total_sketched_kmers / total_reads << " per read)" << endl;
	cerr << " | Kmer matches:          " << total_matches << " (" << 1.0*total_matches / total_reads << " per read)" << endl;
	cerr << " | Seed limit reached:    " << mapper.seeds_limit_reached << " (" << 100.0 * mapper.seeds_limit_reached / total_reads << "%)" << endl;
	cerr << " | Matches limit reached: " << mapper.matches_limit_reached << " (" << 100.0 * mapper.matches_limit_reached / total_reads << "%)" << endl;
	cerr << " | Unmapped reads:        " << unmapped_reads << " (" << 100.0 * unmapped_reads / total_reads << "%)" << endl;
	cerr << " | Average J:             " << total_J / total_mappings << endl;
	cerr << "Total time [sec]:     " << setw(4) << right << T.get_secs("total")     << " (" << setw(6) << right << T.get_secs("total") / total_reads << " per read)" << endl;
	cerr << " | Indexing:          " << setw(4) << right << T.get_secs("indexing")  << " (" << setw(4) << right << T.get_perc("indexing", "total") << "\% of total)" << endl;
	cerr << " | Mapping:           " << setw(4) << right << T.get_secs("mapping")   << " (" << setw(4) << right << T.get_perc("mapping", "total") << "\% of total)" << endl;
	cerr << " |  | read sketching: " << setw(4) << right << T.get_secs("sketching") << " (" << setw(4) << right << T.get_perc("sketching", "mapping") << "\% of mapping)" << endl;
	cerr << " |  | match kmers:    " << setw(4) << right << T.get_secs("matching")  << " (" << setw(4) << right << T.get_perc("matching", "mapping") << "\%)" << endl;
	cerr << " |  | sort matches:   " << setw(4) << right << T.get_secs("sorting")   << " (" << setw(4) << right << T.get_perc("sorting", "mapping") << "\%)" << endl;
	cerr << " |  | sweep:          " << setw(4) << right << T.get_secs("sweep")     << " (" << setw(4) << right << T.get_perc("sweep", "mapping") << "\%)" << endl;
	cerr << " |  | post proc:      " << setw(4) << right << T.get_secs("postproc")  << " (" << setw(4) << right << T.get_perc("postproc", "mapping") << "\%)" << endl;

	return 0;
}
