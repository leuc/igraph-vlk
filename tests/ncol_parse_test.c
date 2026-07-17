/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/ncol_parse.h"
#include "test_utilities.h"

#include <string.h>

static int test_empty_line(void)
{
	char buf[] = "";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(!parse_ncol_line(buf, &n1, &n2, &hw, &w));
	return 0;
}

static int test_whitespace_only_line(void)
{
	char buf[] = "   ";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(!parse_ncol_line(buf, &n1, &n2, &hw, &w));
	return 0;
}

static int test_single_field(void)
{
	char buf[] = "onlyname";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(!parse_ncol_line(buf, &n1, &n2, &hw, &w));
	return 0;
}

static int test_two_fields_no_weight(void)
{
	char buf[] = "a b";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(parse_ncol_line(buf, &n1, &n2, &hw, &w));
	IGRAPH_ASSERT(strcmp(n1, "a") == 0);
	IGRAPH_ASSERT(strcmp(n2, "b") == 0);
	IGRAPH_ASSERT(!hw);
	IGRAPH_ASSERT(w == 0.0);
	return 0;
}

static int test_two_fields_with_weight(void)
{
	char buf[] = "a b 1.5";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(parse_ncol_line(buf, &n1, &n2, &hw, &w));
	IGRAPH_ASSERT(strcmp(n1, "a") == 0);
	IGRAPH_ASSERT(strcmp(n2, "b") == 0);
	IGRAPH_ASSERT(hw);
	IGRAPH_ASSERT(w == 1.5);
	return 0;
}

static int test_negative_weight(void)
{
	char buf[] = "a b -2.5";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(parse_ncol_line(buf, &n1, &n2, &hw, &w));
	IGRAPH_ASSERT(hw);
	IGRAPH_ASSERT(w == -2.5);
	return 0;
}

static int test_tab_separated_fields(void)
{
	char buf[] = "a\tb\t1.0";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(parse_ncol_line(buf, &n1, &n2, &hw, &w));
	IGRAPH_ASSERT(strcmp(n1, "a") == 0);
	IGRAPH_ASSERT(strcmp(n2, "b") == 0);
	IGRAPH_ASSERT(hw);
	IGRAPH_ASSERT(w == 1.0);
	return 0;
}

static int test_malformed_weight(void)
{
	char buf[] = "a b notanumber";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(!parse_ncol_line(buf, &n1, &n2, &hw, &w));
	return 0;
}

static int test_weight_trailing_garbage(void)
{
	char buf[] = "a b 1.5x";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(!parse_ncol_line(buf, &n1, &n2, &hw, &w));
	return 0;
}

static int test_too_many_fields(void)
{
	char buf[] = "a b 1.5 extra";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(!parse_ncol_line(buf, &n1, &n2, &hw, &w));
	return 0;
}

static int test_extra_whitespace_between_fields(void)
{
	char buf[] = "  a    b  ";
	char *n1, *n2;
	bool hw;
	double w;
	IGRAPH_ASSERT(parse_ncol_line(buf, &n1, &n2, &hw, &w));
	IGRAPH_ASSERT(strcmp(n1, "a") == 0);
	IGRAPH_ASSERT(strcmp(n2, "b") == 0);
	IGRAPH_ASSERT(!hw);
	return 0;
}

int main(void)
{
	RUN_TEST(test_empty_line);
	RUN_TEST(test_whitespace_only_line);
	RUN_TEST(test_single_field);
	RUN_TEST(test_two_fields_no_weight);
	RUN_TEST(test_two_fields_with_weight);
	RUN_TEST(test_negative_weight);
	RUN_TEST(test_tab_separated_fields);
	RUN_TEST(test_malformed_weight);
	RUN_TEST(test_weight_trailing_garbage);
	RUN_TEST(test_too_many_fields);
	RUN_TEST(test_extra_whitespace_between_fields);
	return 0;
}
