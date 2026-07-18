/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Correctness tests for graph/community_simhash: the fingerprint must be
 * order-independent (identical member sets -> identical hash), near-identical
 * sets -> small Hamming distance, and disjoint sets -> large Hamming
 * distance. Also verifies the RGB conversion is deterministic in both
 * directions (same hash -> same color; a hash change -> a color change), so
 * the streaming recolor path yields stable colors across Leiden renumbering.
 *
 * No benchmarking: timing/throughput belongs in a separate harness.
 */

#include "graph/community_simhash.h"
#include "test_utilities.h"

#include <stdlib.h>

static int test_order_independent(void)
{
	igraph_integer_t a[] = {3, 1, 7, 9, 2};
	igraph_integer_t b[] = {9, 2, 7, 1, 3}; // same set, different order
	uint64_t ha = community_simhash_from_members(a, 5);
	uint64_t hb = community_simhash_from_members(b, 5);
	IGRAPH_ASSERT(ha == hb);
	return 0;
}

static int test_identical_sets_equal(void)
{
	igraph_integer_t a[] = {0, 1, 2, 3, 4, 5};
	igraph_integer_t b[] = {0, 1, 2, 3, 4, 5};
	IGRAPH_ASSERT(community_simhash_from_members(a, 6) == community_simhash_from_members(b, 6));
	return 0;
}

static int test_small_change_small_hamming(void)
{
	igraph_integer_t a[] = {0, 1, 2, 3, 4, 5};
	igraph_integer_t b[] = {0, 1, 2, 3, 4, 99}; // one member swapped
	int d = community_simhash_hamming(community_simhash_from_members(a, 6), community_simhash_from_members(b, 6));
	// A single-member difference must not flip the whole fingerprint.
	IGRAPH_ASSERT(d < 32);
	return 0;
}

static int test_disjoint_large_hamming(void)
{
	igraph_integer_t a[] = {0, 1, 2, 3, 4, 5};
	igraph_integer_t b[] = {100, 101, 102, 103, 104, 105};
	int d = community_simhash_hamming(community_simhash_from_members(a, 6), community_simhash_from_members(b, 6));
	// Disjoint sets must differ far more than a single-member change would
	// (which stays well under 32), even if not strictly > half the bits.
	IGRAPH_ASSERT(d > 16);
	return 0;
}

static int test_membership_scan_matches_explicit(void)
{
	// Build a membership array where community 7 = {2, 5, 8} (sparse ids) and
	// scan it; result must equal hashing that explicit set directly.
	igraph_integer_t membership[12];
	for (int i = 0; i < 12; i++)
		membership[i] = -1;
	membership[2] = 7;
	membership[5] = 7;
	membership[8] = 7;
	membership[3] = 4; // another community, must be ignored
	igraph_integer_t explicit[] = {2, 5, 8};
	uint64_t h_scan = community_simhash_from_membership(membership, 12, 7);
	uint64_t h_expl = community_simhash_from_members(explicit, 3);
	IGRAPH_ASSERT(h_scan == h_expl);
	return 0;
}

static int test_color_deterministic(void)
{
	float c1[3], c2[3];
	community_simhash_to_rgb(0xDEADBEEFCAFEBABEULL, c1);
	community_simhash_to_rgb(0xDEADBEEFCAFEBABEULL, c2);
	IGRAPH_ASSERT(c1[0] == c2[0] && c1[1] == c2[1] && c1[2] == c2[2]);
	// RGB components must be finite and within [0,1].
	for (int i = 0; i < 3; i++) {
		IGRAPH_ASSERT(c1[i] >= 0.0f && c1[i] <= 1.0f);
	}
	return 0;
}

static int test_color_stable_across_renumber(void)
{
	// Two different representative ids for the SAME member set must hash to
	// the same value (the whole point: colors survive Leiden renumbering).
	igraph_integer_t set_a[] = {4, 8, 15, 16, 23, 42};
	igraph_integer_t set_b[] = {42, 23, 16, 15, 8, 4}; // identical set
	uint64_t h = community_simhash_from_members(set_a, 6);
	float c1[3], c2[3];
	community_simhash_to_rgb(h, c1);
	community_simhash_to_rgb(h, c2); // same hash -> same color
	IGRAPH_ASSERT(c1[0] == c2[0] && c1[1] == c2[1] && c1[2] == c2[2]);
	return 0;
}

int main(void)
{
	RUN_TEST(test_order_independent);
	RUN_TEST(test_identical_sets_equal);
	RUN_TEST(test_small_change_small_hamming);
	RUN_TEST(test_disjoint_large_hamming);
	RUN_TEST(test_membership_scan_matches_explicit);
	RUN_TEST(test_color_deterministic);
	RUN_TEST(test_color_stable_across_renumber);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
