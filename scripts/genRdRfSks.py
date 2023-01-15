#!/usr/bin/env python3

import argparse as args
from CalcLocInterSim import calcSketch
from MutateSeq import mutateSeq, NUCL_ALPHABET
from random import randrange, choice, seed
from sys import maxsize
from math import floor
from numpy import unique

#This script randomly generates a pair of sequences where one has been mutated according to a mutation process from the other. 
#Afterwards a sequencing process was simulated by introducing some additional errors. Sketches are calculated for both sequences and
#outputted.
"python3 scripts/genRdRfSks.py -s {params.seed} -l {params.sl} -m {params.sr} -i {params.ir} -d {params.dr} -se " + \
		"{params.ser} -ie {params.ier} -de {params.der} -k {params.k} -r {params.hRat} > {output}"

if __name__ == '__main__':
	#Setting up the argument parser
	parser = args.ArgumentParser(description="This script generates sketches of a sequence and its mutated copy in .sk format.")
	parser.add_argument('-l', metavar='SeqLen', type=int, required=True, help="Length of sequences")
	parser.add_argument('-m', metavar='SubRate', type=float, required=True, help="Substitution rate")
	parser.add_argument('-d', metavar='DelRate', type=float, required=True, help="Deletion rate")
	parser.add_argument('-i', metavar='InsLen', type=float, required=True, help="Mean insertion length")
	parser.add_argument('-se', metavar='SubErr', type=float, required=True, help="Substitution error")
	parser.add_argument('-de', metavar='DelErr', type=float, required=True, help="Deletion error")
	parser.add_argument('-ie', metavar='InsErr', type=float, required=True, help="Mean insertion length error")
	parser.add_argument('-s', metavar='Seed', type=int, default=randrange(maxsize), help="Random seed to use")
	parser.add_argument('-k', metavar='KmerLen', type=int, default=9, help="The k-mer length to use")
	parser.add_argument('-r', metavar='HashRatio', type=float, default=0.1, help="The ratio of hash values from the set of all " + \
		"possible hash values to be included into a sketch")

	arguments = parser.parse_args()

	#Check if rates and insertion length were chosen reasonably
	if arguments.m < 0 or arguments.m > 1:
		print("ERROR: Substitution rate should be between 0 and 1", file=stderr)

	if arguments.d < 0 or arguments.d > 1:
		print("ERROR: Deletion rate should be between 0 and 1", file=stderr)

	if arguments.i < 0:
		print("ERROR: Mean insertion length should be positive", file=stderr)

	#Generate sequence whose sketch has no duplicates
	seed(arguments.s)
	minHashThres = floor(((4 ** arguments.k) - 1) * arguments.r)

	seq = ""

	for i in range(arguments.l):
		seq += choice(NUCL_ALPHABET)

	sketch = calcSketch(seq, arguments.k, minHashThres)

	#Testing
	# print("Initial sequence generated")

	#Mutate sequence
	mutSeq = mutateSeq(seq, arguments.m, arguments.d, arguments.i)

	#Testing
	# print("First mutation process complete")

	seqSeq = mutateSeq(mutSeq, arguments.se, arguments.de, arguments.ie)

	#Testing
	# print("Completed second mutation process")

	mutSeqSketch = calcSketch(seqSeq, arguments.k, minHashThres)

	#Output sketches
	print(f">SketchPair_{arguments.s} original template")
	print(' '.join([str(h) for h in sketch]))
	print(f">SketchPair_{arguments.s} mutated and sequenced template")
	print(' '.join([str(h) for h in mutSeqSketch]))