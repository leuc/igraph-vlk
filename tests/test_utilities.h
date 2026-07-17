/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared helpers for igraph-vlk unit tests, following the conventions of
 * igraph's own test suite (tests/unit/test_utilities.h): RUN_TEST drives each
 * test function and verifies the FINALLY stack afterwards, and tests assert
 * with IGRAPH_ASSERT.
 */

#ifndef TEST_UTILITIES_H
#define TEST_UTILITIES_H

#include <igraph.h>
#include <stdio.h>

/* Assert the igraph FINALLY stack is empty after a test (catches cleanup
 * mismatches). Mirrors igraph's VERIFY_FINALLY_STACK(). */
#define VERIFY_FINALLY_STACK() \
	do { \
		if (!IGRAPH_FINALLY_STACK_EMPTY) { \
			fprintf(stderr, "FINALLY stack not empty (size %d) at %s:%d\n", IGRAPH_FINALLY_STACK_SIZE(), __FILE__, __LINE__); \
			return 1; \
		} \
	} while (0)

/* Run a test function; bail out on the first nonzero return and verify the
 * FINALLY stack. Requires an int `retval` in the enclosing scope. */
#define RUN_TEST(func) \
	do { \
		int retval = func(); \
		if (retval) { \
			fprintf(stderr, "test failed: %s\n", #func); \
			return retval; \
		} \
		VERIFY_FINALLY_STACK(); \
	} while (0)

#endif // TEST_UTILITIES_H
