/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/display_hdr.h"
#include "test_utilities.h"

static int test_hdr10_requires_pq_and_bt2020(void)
{
	IGRAPH_ASSERT(display_hdr_description_is_hdr10(true, true));
	IGRAPH_ASSERT(!display_hdr_description_is_hdr10(true, false));
	IGRAPH_ASSERT(!display_hdr_description_is_hdr10(false, true));
	IGRAPH_ASSERT(!display_hdr_description_is_hdr10(false, false));
	return 0;
}

static int test_bt2020_named_coordinates(void)
{
	IGRAPH_ASSERT(display_hdr_is_bt2020_primaries(708000, 292000, 170000, 797000, 131000, 46000, 312700, 329000));
	IGRAPH_ASSERT(display_hdr_is_bt2020_primaries(710000, 290000, 172000, 795000, 129000, 48000, 314700, 327000));
	return 0;
}

static int test_non_bt2020_coordinates(void)
{
	IGRAPH_ASSERT(!display_hdr_is_bt2020_primaries(640000, 330000, 300000, 600000, 150000, 60000, 312700, 329000));
	IGRAPH_ASSERT(!display_hdr_is_bt2020_primaries(711000, 292000, 170000, 797000, 131000, 46000, 312700, 329000));
	return 0;
}

static int test_status_change_consumed_once(void)
{
	WindowState window = {.displayColor = {.known = true, .hdr10 = true, .reference_luminance = 203.0f, .revision = 42}, .displayColorDirty = true};
	DisplayColorInfo info = {0};
	IGRAPH_ASSERT(window_consume_display_color_info(&window, &info));
	IGRAPH_ASSERT(info.known);
	IGRAPH_ASSERT(info.hdr10);
	IGRAPH_ASSERT(info.reference_luminance == 203.0f);
	IGRAPH_ASSERT(info.revision == 42);
	IGRAPH_ASSERT(!window_consume_display_color_info(&window, &info));
	return 0;
}

int main(void)
{
	RUN_TEST(test_hdr10_requires_pq_and_bt2020);
	RUN_TEST(test_bt2020_named_coordinates);
	RUN_TEST(test_non_bt2020_coordinates);
	RUN_TEST(test_status_change_consumed_once);
	return 0;
}
