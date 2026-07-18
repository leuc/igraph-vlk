/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * See include/graph/community_simhash.h for the design notes.
 */

#include "graph/community_simhash.h"

#include <math.h>
#include <stdint.h>

// Two independent 64-bit integer hashes (SplitMix64-style) used to project a
// member id onto each SimHash bit's sign. Different seeds give independent
// projections so the 64 bits are decorrelated.
static inline uint64_t simhash_mix(uint64_t x, uint64_t seed)
{
	uint64_t z = (x + seed) * 0x9E3779B97F4A7C15ULL;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

uint64_t community_simhash_from_members(const igraph_integer_t *members, int count)
{
	if (!members || count <= 0)
		return 0;

	// Signed 64-bit accumulator per bit: bit b is set iff the accumulated
	// sign over all members is non-negative. Identical member sets give an
	// identical accumulator regardless of order.
	int64_t acc[64];
	for (int b = 0; b < 64; b++)
		acc[b] = 0;

	for (int i = 0; i < count; i++) {
		uint64_t id = (uint64_t)members[i];
		for (int b = 0; b < 64; b++) {
			uint64_t h1 = simhash_mix(id, 0x100000000ULL + (uint64_t)b * 0x9E3779B9ULL);
			uint64_t h2 = simhash_mix(id, 0x200000000ULL + (uint64_t)b * 0x85EBCA77ULL);
			// signed comparison of the two projections
			acc[b] += (int64_t)(h1 > h2 ? 1 : -1);
		}
	}

	uint64_t hash = 0;
	for (int b = 0; b < 64; b++)
		if (acc[b] >= 0)
			hash |= (1ULL << b);
	return hash;
}

uint64_t community_simhash_from_membership(const igraph_integer_t *membership, igraph_integer_t vcount, igraph_integer_t comm_id)
{
	if (!membership || vcount <= 0)
		return 0;
	// Single pass over all vertices: collect this community's member ids into
	// a stack buffer (at most vcount entries) and hash the set. O(vcount),
	// no allocation beyond a transient scratch array.
	igraph_integer_t *buf = malloc((size_t)vcount * sizeof(igraph_integer_t));
	if (!buf)
		return 0;
	int n = 0;
	for (igraph_integer_t v = 0; v < vcount; v++)
		if (membership[v] == comm_id)
			buf[n++] = v;
	uint64_t hash = community_simhash_from_members(buf, n);
	free(buf);
	return hash;
}

int community_simhash_hamming(uint64_t a, uint64_t b)
{
	return __builtin_popcountll(a ^ b);
}

void community_simhash_to_rgb(uint64_t hash, float out_rgb[3])
{
	// Golden-ratio hue stepping, identical to community_id_to_rgb(), but fed
	// the 64-bit SimHash instead of a volatile comm_id. The hash is treated
	// as a unit-fraction hue in [0,1).
	float hue = (float)(hash >> 11) / (float)(1ULL << 53);
	hue -= floorf(hue);
	float h = hue * 6.0f;
	int hi = (int)floorf(h);
	float f = h - hi;
	out_rgb[0] = (hi == 0 || hi == 5) ? 1.0f : (hi == 1 || hi == 2) ? 1.0f - f : f;
	out_rgb[1] = (hi == 0 || hi == 3) ? f : (hi == 1 || hi == 2) ? 1.0f : 1.0f - f;
	out_rgb[2] = (hi == 0 || hi == 4) ? 1.0f - f : (hi == 2 || hi == 3) ? f : 1.0f;
}
