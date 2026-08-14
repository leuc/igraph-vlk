/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * See include/graph/community_simhash.h for the design notes.
 */

#include "graph/community_simhash.h"

#include "graph/graph_color.h"

#include <stdint.h>
#include <stdlib.h>

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

void community_simhash_batch(const igraph_integer_t *membership, igraph_integer_t vcount, const igraph_integer_t *comm_ids, int num_comm_ids, uint64_t *out)
{
	if (!membership || vcount <= 0 || !comm_ids || num_comm_ids <= 0 || !out)
		return;

	// comm_id -> compact index into acc[]/comm_ids[], -1 if not requested.
	int *comm_to_idx = malloc((size_t)vcount * sizeof(int));
	if (!comm_to_idx)
		return;
	for (igraph_integer_t c = 0; c < vcount; c++)
		comm_to_idx[c] = -1;
	for (int i = 0; i < num_comm_ids; i++) {
		igraph_integer_t c = comm_ids[i];
		if (c >= 0 && c < vcount)
			comm_to_idx[c] = i;
	}

	// One signed accumulator per (requested community, bit), same projection
	// as community_simhash_from_members's per-member loop.
	int64_t *acc = calloc((size_t)num_comm_ids * 64, sizeof(int64_t));
	if (!acc) {
		free(comm_to_idx);
		return;
	}

	for (igraph_integer_t v = 0; v < vcount; v++) {
		igraph_integer_t c = membership[v];
		if (c < 0 || c >= vcount)
			continue;
		int idx = comm_to_idx[c];
		if (idx < 0)
			continue;
		uint64_t id = (uint64_t)v;
		int64_t *bits = &acc[(size_t)idx * 64];
		for (int b = 0; b < 64; b++) {
			uint64_t h1 = simhash_mix(id, 0x100000000ULL + (uint64_t)b * 0x9E3779B9ULL);
			uint64_t h2 = simhash_mix(id, 0x200000000ULL + (uint64_t)b * 0x85EBCA77ULL);
			bits[b] += (int64_t)(h1 > h2 ? 1 : -1);
		}
	}

	for (int i = 0; i < num_comm_ids; i++) {
		int64_t *bits = &acc[(size_t)i * 64];
		uint64_t hash = 0;
		for (int b = 0; b < 64; b++)
			if (bits[b] >= 0)
				hash |= (1ULL << b);
		out[comm_ids[i]] = hash;
	}

	free(acc);
	free(comm_to_idx);
}

int community_simhash_hamming(uint64_t a, uint64_t b)
{
	return __builtin_popcountll(a ^ b);
}

GraphColor community_simhash_to_color(uint64_t hash)
{
	return graph_color_generate(hash);
}
