/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef INTERACTION_FILTER_H
#define INTERACTION_FILTER_H

#include "interaction/state.h"

/**
 * Execute function for "Show All" filter command.
 * Resets all node visibility to 1.0f.
 */
void execute_filter_reset(ExecutionContext *ctx);

/**
 * Execute function for attribute filter commands.
 * Reads attr_name/attr_value from IgraphCommand.user_data (FilterContext).
 */
void execute_filter_by_attr(ExecutionContext *ctx);

#endif // INTERACTION_FILTER_H
