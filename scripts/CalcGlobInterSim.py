#!/usr/bin/env python3

import argparse as args
from math import floor
from sys import stdout
from os.path import exists
from Bio import SeqIO
from numpy import unique

#This script calculates the intersection similarity measure (described in the manuscript as of December 31 2022) between a given pair of sketches

if __name__ == '__main__':
	#Setting up the argument parser
	parser = args.ArgumentParser(description="This script calculates the global intersection similarity between a pair of " + \
		"sketches.")
	parser.add_argument('-p', metavar='Pair', type=str, required=True, help="The sketch pair (.sk format) file)")
	parser.add_argument('-c', metavar='CommonWeight', type=int, default=1, help="Weight to reward common hashes (default 1)")
	parser.add_argument('-u', metavar='UniqueWeight', type=int, default=1, help="Weight to punish unique hashes (default 1)")

	arguments = parser.parse_args()

	#Check if given weights are positive
	if arguments.c < 1 or arguments.u < 1:
		print("ERROR: Weights must be positive!", file=stdout)
		exit(-1)

	#Read sketches and count k-mer occurrences
	occ = {}
	sketchCount = -1

	for l in open(arguments.p, 'r'):
		if l.startswith('>'):
			sketchCount += 1

			if sketchCount > 1:
				break
				
			continue

		for k in l.split(' '):
			if not k in occ:
				occ[k] = [0, 0]

			#Testing
			print("sketchCount:", sketchCount)

			occ[k][sketchCount] += 1

	score = 0

	for k in occ:
		kMin = min(occ[k][:2])
		score += kMin - arguments.u * (max(occ[k][:2]) - kMin)

	print(score)
