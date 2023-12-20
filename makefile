CC = g++
CXX_STANDARD = -std=c++2a
CFLAGS = -g -O3 -march=native -lm -lpthread -Wall -Wextra -Wno-unused-parameter  -Wno-unused-result 
LIBS = minimap2/libminimap2.a -lz
DEPFLAGS = -MMD -MP

SRCS = src/sweep.cpp src/IO.h src/Sketch.h
SWEEP_BIN = ./sweep
MINIMAP_BIN = minimap2
MAPQUIK_BIN = mapquik
ESKEMAP_BIN = eskemap
EDLIB_BIN = ~/libs/edlib/build/bin/edlib-aligner

REF = newevals/chm13.fa
READS = newevals/reads-chm13-positive.fa
#REF = newevals/t2tChrY.fa
#READS = newevals/reads-ChrY-positive.fa

MAX_SEEDS = 10000
MAX_MATCHES = 100 300 1000 3000 10000 30000 100000
#10000000

all: sweep

$(SWEEP_BIN): $(SRCS)
	$(CC) $(CXX_STANDARD) $(CFLAGS) $< -o $@ $(LIBS)

gen_reads:
	cd newevals; \
	./simulate_reads.sh

eval_abe: sweep
	mkdir -p ../out
#	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 >out/sweep.out

	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -z out/sweep-b.params >out/sweep-b.out
	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -a extend_equally -z out/sweep-b-a.params >out/sweep-b-a.out
	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -e consecutive -z out/sweep-b-e.params >out/sweep-b-e.out
	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -e consecutive -a extend_equally -z out/sweep-b-e-a.params >out/sweep-b-e-a.out

	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -z out/sweep.params >out/sweep.out
	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -a extend_equally -z out/sweep-a.params >out/sweep-a.out
	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -e consecutive -z out/sweep-e.params >out/sweep-e.out
	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -e consecutive -a extend_equally -z out/sweep-e-a.params >out/sweep-e-a.out

eval_multiple_alignments: sweep
#	time $(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -a fine 		     -z out/sweep.params >out/sweep.out
	time $(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -a fine -x		     -z out/sweep-x.params >out/sweep-x.out
#	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -a extend_equally -x -z out/sweep-b-a.params >out/sweep-b-a-x.out
#	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -a extend_equally -z out/sweep-b-a.params >out/sweep-b-a.out 2>out/sweep-b-a.cerr
#	$(SWEEP_BIN) -s $(REF) -p $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -a extend_equally -o -z out/sweep-b-a.params >out/sweep-b-a-o.out 2>out/sweep-b-a-o.cerr

debug: sweep
	$(SWEEP_BIN) -s $(REF) -p simulations/reads/1.fasta $(READS) -k 15 -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -a extend_equally -z out/sweep-1.params >out/sweep-1.out 2>out/sweep-1.cerr

eval_sweep_max_seeds: sweep
	@DIR=newevals/eval_sweep-K22; \
	mkdir -p $${DIR}; \
	for maxseeds in $(MAX_SEEDS); do \
		for maxmatches in $(MAX_MATCHES); do \
			f=$${DIR}/"sweep-S$${maxseeds}-M$${maxmatches}"; \
			echo "Processing $${f}"; \
			time $(SWEEP_BIN) -s $(REF) -p $(READS) -t 0.0 -x -k 22 -z -S $${maxseeds} -M $${maxmatches} $${f}.params >$${f}.paf 2>$${f}.log; \
			paftools.js mapeval $${f}.paf | tee $${f}.eval; \
		done \
    done

run_sweep: sweep
	time $(SWEEP_BIN) -s $(REF) -p $(READS) -t 0.0 -x -k 22 -z -S 1000 -M 10000 newevals/out/sweep.params >newevals/out/sweep.paf 2>newevals/out/sweep.log
	paftools.js mapeval newevals/out/sweep.paf | tee newevals/out/sweep.eval

run_minimap:
	time $(MINIMAP_BIN) -x map-hifi -t 1 $(REF) $(READS) >out/minimap-Y.paf
	paftools.js mapeval out/minimap-Y.paf | tee out/minimap-Y.eval

run_mapquik:
	time $(MAPQUIK_BIN) $(READS) --reference $(REF) --threads 1 -p out/mapquik-Y
	paftools.js mapeval out/mapquik-Y.paf | tee out/mapquik-Y.eval

run_eskemap:
	time $(ESKEMAP_BIN) -p $(READS) -s $(REF) -b highAbundKmersMiniK15w10Lrgr100BtStrnds.txt -N >out/eskemap-Y.out

run: run_sweep run_minimap run_mapquik

clean:
	rm -f sweep
