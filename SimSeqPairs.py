#!/usr/bin/env python3

import argparse as args
from random import seed, choice, random, randrange

#This script generates a given number of sequence pairs where one sequence is derived from the other after applying substitions and indels according 
#to some rates.

#Some constants#
#The nucleotide alphabet
NUCL_ALPHABET = "ACGT"
#Substitution alphabets
SUB_BASES = {'A': "CGT", 'C': "AGT", 'G': "ACT", 'T': "ACG"}

#Setting up the argument parser
parser = args.ArgumentParser(description="This script generates a given number of random DNA sequences and mutates it resulting in a second sequence.")
parser.add_argument('-s', metavar='Seed', type=int, default=42, help="The random seed to be used")
parser.add_argument('-n', metavar='NbSeqs', type=int, required=True, help="Number of sequences to generate")
parser.add_argument('-l', metavar='SeqLen', type=int, required=True, help="Length of sequences to generate")
parser.add_argument('-m', metavar='SubsRate', type=float, default=0.1, help="Substitution rate")
parser.add_argument('-i', metavar='InsRate', type=float, default=0.1, help="Insertion rate")
parser.add_argument('-d', metavar='DelRate', type=float, default=0.1, help="Deletion rate")

arguments = parser.parse_args()

#Initialize with random seed
seed(arguments.s)

#Generate sequence pairs
for i in range(arguments.n):
	#Generate the first sequence
	randomSeq = ""

	for j in range(arguments.l):
		randomSeq += choice(NUCL_ALPHABET)

	#Mutate the sequence
	mutatedSeq = randomSeq

	for j in range(arguments.l):
		#Check if a substitution takes place in this iteration
		if random() < arguments.m:
			#Decide for a position inside the sequence
			pos = randrange(len(mutatedSeq))
			#Introduce a substitution at a random position
			mutatedSeq = mutatedSeq[:pos] + choice(SUB_BASES[mutatedSeq[pos]]) + mutatedSeq[pos + 1:]

		#Check if an insertion takes place in this iteration
		if random() < arguments.i:
			#Decide for a position inside the sequence
			pos = randrange(len(mutatedSeq))
			#Insert a random base and prune the sequence
			mutatedSeq = mutatedSeq[:pos] + choice(NUCL_ALPHABET) + mutatedSeq[pos:len(mutatedSeq) - 1]

		#Check if a deletion takes place in this iteration
		if random() < arguments.d:
			#Decide for a position inside the sequence
			pos = randrange(len(mutatedSeq))
			#Delete a base and append a random one
			mutatedSeq = mutatedSeq[:pos] + mutatedSeq[pos + 1:] + choice(NUCL_ALPHABET)

	#Output sequence pair
	print(randomSeq, mutatedSeq)
		