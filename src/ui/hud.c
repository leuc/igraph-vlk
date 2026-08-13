/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ui/hud.h"
#include "vulkan/renderer_ui.h"
#include <stdio.h>
#include <string.h>

void ui_hud_init(void)
{
	// No initialization needed currently
}

void ui_hud_update(AppState *state, float fps)
{
	char buf[1024];
	char menu_state[512] = "";
	if (state->app_ctx.current_state == STATE_MENU_OPEN) {
		snprintf(menu_state, 512, " [MENU:%d]", state->renderer.menu.visible ? state->renderer.menu.node_count : 0);
	} else if (state->app_ctx.current_state == STATE_AWAITING_SELECTION)
		strcpy(menu_state, " [PICKING]");
	else if (state->app_ctx.current_state == STATE_EXECUTING)
		strcpy(menu_state, " [RUNNING]");

	// Add background job status if in progress
	if (state->job_in_progress) {
		snprintf(menu_state + strlen(menu_state), 512 - strlen(menu_state), " [%s:%.0f%%]", state->job_status_message, state->job_progress * 100.0f);
	}

	snprintf(buf, sizeof(buf),
			 "[N]ode:%d [E]dge:%d Filter:1-9 [K]Core:%d "
			 "[R]eset [H]ide FPS:%.1f%s",
			 state->current_graph.props.node_count, state->current_graph.props.edge_count, state->current_graph.props.coreness_filter, fps, menu_state);

	renderer_update_ui(&state->renderer, buf);
}
