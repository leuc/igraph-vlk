/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Order-independent SimHash of a community's member-vertex set.
 *
 * A community's color (and, separately, its change-detection fingerprint)
 * must not depend on the volatile representative vertex id that dynamic
 * Leiden assigns as the community label — that id changes whenever
 * communities merge or split, which would otherwise make colors thrash on
 * every streaming poll. Instead we hash the *set of member vertex ids*.
 *
 * The fingerprint is order-independent: identical member sets produce a
 * bit-identical 64-bit hash regardless of enumeration order, and sets that
 * differ by only a few members produce hashes at small Hamming distance
 * (the defining SimHash property). We implement it as a bit-vector SimHash:
 * for each of 64 bits we project every member id through two independent
 * hashes, accumulate the sign of their difference, and set the bit from the
 * sign. This is O(members) and needs no sorting of the member list.
 */

#ifndef GRAPH_COMMUNITY_SIMHASH_H
#define GRAPH_COMMUNITY_SIMHASH_H

#include <igraph/igraph.h>

// 64-bit order-independent fingerprint of an explicit community member list.
uint64_t community_simhash_from_members(const igraph_integer_t *members, int count);
// 64-bit fingerprint of the community comm_id, found by scanning the full
// membership array for vertices whose community equals comm_id (O(vcount)).
// Avoids reaching into dynamic-Leiden's internal linked lists.
uint64_t community_simhash_from_membership(const igraph_integer_t *membership, igraph_integer_t vcount, igraph_integer_t comm_id);
// Computes the SimHash of every community listed in comm_ids in a single
// O(vcount) pass over membership (vs. calling community_simhash_from_membership
// once per community, which is O(vcount) per call — O(vcount * num_comm_ids)
// overall). Uses the same projection as community_simhash_from_members, so
// results are bit-identical to hashing each community individually. Writes
// out[comm_ids[i]] for each i; entries not listed in comm_ids are left
// untouched.
void community_simhash_batch(const igraph_integer_t *membership, igraph_integer_t vcount, const igraph_integer_t *comm_ids, int num_comm_ids, uint64_t *out);
// Hamming distance between two SimHashes (number of differing bits).
int community_simhash_hamming(uint64_t a, uint64_t b);

// Deterministic color from a SimHash, using the same golden-ratio hue
// stepping + HSV->RGB conversion as community_id_to_rgb(). Feeds the 64-bit
// hash instead of a volatile comm_id, so a stable community keeps its color.
void community_simhash_to_rgb(uint64_t hash, float out_rgb[3]);

#endif // GRAPH_COMMUNITY_SIMHASH_H
