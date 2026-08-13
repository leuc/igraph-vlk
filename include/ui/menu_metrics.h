/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef UI_MENU_METRICS_H
#define UI_MENU_METRICS_H

typedef struct
{
	float item_height;
	float title_height;
	float text_padding;
	float text_scale;
	float branch_label_extra_width;
	float card_horizontal_padding;
	float arrow_right_inset;
} MenuMetrics;

extern const MenuMetrics MENU_METRICS;

#endif
