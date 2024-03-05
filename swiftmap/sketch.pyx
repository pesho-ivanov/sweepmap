# distutils: language=c++

cimport cython

from libc cimport stdint
from libcpp.vector cimport vector
from libcpp.unordered_map cimport unordered_map
from libcpp.bit cimport rotl, rotr, countr_zero

ctypedef stdint.uint64_t hash_t
ctypedef stdint.int32_t pos_t
ctypedef stdint.int8_t segm_t

cdef struct Kmer:
    pos_t pos
    hash_t hash
    bint strand

ctypedef vector[Kmer] sketch_t

# TODO: Meaningful names
cdef hash_t[256] LUT_fw, LUT_rc

# TODO: What the hell is LUT
cdef initialize_lut():
    # https://gist.github.com/Daniel-Liu-c0deb0t/7078ebca04569068f15507aa856be6e8
    LUT_fw['a'] = LUT_fw['A'] = 0x3c8bfbb395c60474
    LUT_fw['c'] = LUT_fw['C'] = 0x3193c18562a02b4c
    LUT_fw['g'] = LUT_fw['G'] = 0x20323ed082572324
    LUT_fw['t'] = LUT_fw['T'] = 0x295549f54be24456
    LUT_rc['a'] = LUT_rc['A'] = LUT_fw['T']
    LUT_rc['c'] = LUT_rc['C'] = LUT_fw['G']
    LUT_rc['g'] = LUT_rc['G'] = LUT_fw['C']
    LUT_rc['t'] = LUT_rc['T'] = LUT_fw['A']

@cython.boundscheck(False)
@cython.wraparound(False)
cpdef sketch_t build_fmh_sketch(const unsigned char[:] s, int k, double h_frac):
    initialize_lut()

    cdef size_t length = s.shape[0]
    cdef sketch_t kmers
    kmers.reserve(<size_t>(1.1 * length * h_frac))

    if length < k:
        return kmers

    cdef hash_t h, h_fw = 0, h_rc = 0
    cdef hash_t h_thresh = <hash_t>(h_frac * stdint.UINT64_MAX)
    cdef int r

    for r in range(k):
        h_fw ^= rotl(LUT_fw[s[r]], k-r-1)
        h_rc ^= rotl(LUT_rc[s[r]], r)

    cdef hash_t first_diff_bit
    cdef bint strand

    while True:
        # HACK! the lowest differing bit is not expected to correlate much with (h < hThres)
        # TODO: tie break

        first_diff_bit = 1 << countr_zero(h_fw ^ h_rc)
        strand = h_fw & first_diff_bit
        
        h = h_rc if strand else h_fw

        if h < h_thresh:
            kmers.push_back(
                Kmer(pos=r-k+1, hash=h, strand=strand)
            )

        if r >= length:
            break

        h_fw = rotl(h_fw, 1) ^ rotl(LUT_fw[s[r-k]], k) ^ LUT_fw[s[r]]
        h_rc = rotr(h_rc, 1) ^ rotr(LUT_rc[s[r-k]], 1) ^ rotl(LUT_rc[s[r]], k-1)

        r += 1

    return kmers

cdef class Sketch:
    cdef sketch_t _sketch

    def __init__(self, const unsigned char[:] s, int k, double h_frac):
        self._sketch = build_fmh_sketch(s, k, h_frac)

    def state(self):
        return self._sketch


cdef struct RefPos:
    segm_t segm
    pos_t pos


cdef class Index:
    cdef vector[sketch_t] segments
    cdef unordered_map[hash_t, RefPos] hit_first
    cdef unordered_map[hash_t, vector[RefPos]] hit_rest

    cdef _add(self, sketch_t sketch):
        cdef size_t segm_id = self.segments.size()
        self.segments.push_back(sketch)
        for pos in range(sketch.size()):
            kmer = sketch[pos]
            hit = RefPos(segm_id, pos)
            if not self.hit_first.contains(kmer.hash):
                self.hit_first[kmer.hash] = hit
            else:
                self.hit_rest[kmer.hash].push_back(hit)

    def add(self, Sketch sketch):
        self._add(sketch._sketch)

    def state(self):
        return (self.segments, self.hit_first, self.hit_rest)


cdef struct Mapping:
    segm_t segm
    pos_t pos
    

cdef vector[Mapping] sweep_map(Index reference_index, sketch_t pattern_sketch, max_hits):
    cdef vector[Mapping] mapping

    # get [(hash, hits_in_index)]

    # leave only the first max_hits with the lowest hits: selected_hashes

    # build [(hash, ref_pos)] with only selected_hashes
    # sort it by ref_pos

    # run sweep, collect mapping

    mapping.push_back(Mapping(0, 0))
    return mapping
