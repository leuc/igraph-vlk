#pragma once

#include <stdbool.h>
#include "app_state.h"

/**
 * Initialize the HUD system.
 */
void ui_hud_init(void);

/**
 * Update HUD text with current application state.
 * @param state Pointer to the application state
 * @param fps Current frames per second
 */
void ui_hud_update(AppState *state, float fps);

