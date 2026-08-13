/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef INTERACTION_STATE_H
#define INTERACTION_STATE_H

#include "interaction/spatial.h"
#include <cglm/cglm.h>
#include <igraph.h>
#include <stdbool.h>
#include <stdint.h>

struct CommandDef;

typedef enum {
	PARAM_TYPE_INT,
	PARAM_TYPE_FLOAT,
	PARAM_TYPE_REAL,
	PARAM_TYPE_BOOL,
	PARAM_TYPE_NODE_SELECTION, // Requires 3D raycast picking
	PARAM_TYPE_EDGE_SELECTION, // Requires 3D raycast picking
	PARAM_TYPE_ENUM,		   // E.g., choosing a layout algorithm
	PARAM_TYPE_STRING		   // String parameter (e.g., network ID)
} ParameterType;

typedef struct
{
	const char *name;
	ParameterType type;
	union {
		int i_val;
		igraph_real_t f_val;
		bool b_val;
		igraph_integer_t selection_id; // Stores the picked igraph_integer_t vertex/edge ID
		const char *str_val;		   // String parameter (owned by caller, not by CommandParameter)
	} value;
	// Limits for UI sliders/forms
	igraph_real_t min_val;
	igraph_real_t max_val;
} CommandParameter;

typedef struct
{
	CommandParameter *params;
	int num_params;
	// Callbacks for UI updates
	void (*update_visuals_callback)(void);
	// Pointer to full application state for layout updates
	struct AppState *app_state;
	// Cancellation flag set by main thread to abort worker
	bool running;
	// Transition duration for layout morphing (0 = snap, >0 = smooth transition)
	float transition_duration;
} ExecutionContext;

typedef struct
{
	const char *id_name;
	const char *display_name;
	int num_params;
	CommandParameter *params;		  // Array of required parameters
	const struct CommandDef *cmd_def; // Pointer to command definition in registry
} IgraphCommand;

typedef enum {
	NODE_BRANCH,	   // Opens a submenu card
	NODE_LEAF_COMMAND, // Standard clickable action button
} MenuNodeType;

typedef struct MenuNode
{
	MenuNodeType type;
	const char *label;

	vec3 quad_center_pos;
	versor rotation;
	vec3 right_vec;
	vec3 up_vec;
	float box_width;
	float box_height;

	float card_width;
	float card_height;
	vec3 card_bg_pos;

	int num_children;
	struct MenuNode **children;

	IgraphCommand *command;

	bool is_expanded;
	bool is_visible;
} MenuNode;

typedef struct
{
	char key[32];
	char value[64];
} InfoKeyValuePair;

typedef struct
{
	char title[64];
	int num_pairs;
	InfoKeyValuePair pairs[8];
} InfoCardData;

typedef struct
{
	bool is_visible;
	char title[64];
	int num_pairs;
	InfoKeyValuePair pairs[8];
} InfoCardState;

typedef enum {
	STATE_GRAPH_VIEW,		  // Freely navigating the graph
	STATE_MENU_OPEN,		  // Sphere menu is active, dimming the graph
	STATE_AWAITING_SELECTION, // User must pick nodes/edges with the mouse/laser
	STATE_EXECUTING,		  // Calculating (blocks input or shows progress bar)
	STATE_JOB_IN_PROGRESS,	  // Long-running operation in worker thread
	STATE_DISPLAY_RESULTS	  // Showing execution results (overlays, histograms, etc.)
} AppInteractionState;

typedef enum {
	MENU_INVALIDATE_NONE = 0,
	MENU_INVALIDATE_SCENE = 1 << 0,
	MENU_INVALIDATE_TEXT = 1 << 1,
	MENU_INVALIDATE_LAYOUT = 1 << 2,
} MenuInvalidation;

typedef struct
{
	MenuNode *root;
	MenuNode *active_level;
	MenuNode *hovered_node;
	SpatialBasis spawn_basis;
	InfoCardState info_card;
	bool is_open;
	uint64_t layout_revision;
	uint64_t applied_layout_revision;
	uint64_t text_revision;
	uint64_t scene_revision;
} MenuState;

typedef struct AppContext
{
	AppInteractionState current_state;
	MenuState menu;

	IgraphCommand *pending_command;
	int selection_step; // How many nodes have we picked so far?

	bool has_visual_results;
	void *results_data; // Optional: pointer to results for overlay rendering
} AppContext;

typedef struct AppState AppState;
void update_app_state(AppState *state);
void app_context_init(AppContext *ctx, MenuNode *root_menu);
void app_context_destroy(AppContext *ctx);
void handle_menu_selection(AppContext *app, MenuNode *selected_node);
void check_pending_command_requirements(AppContext *app);
void menu_invalidate(MenuState *menu, MenuInvalidation flags);
bool menu_set_hovered(MenuState *menu, MenuNode *node);
void menu_set_info_card(MenuState *menu, const InfoCardData *data);

void apply_quit(ExecutionContext *ctx, void *result_data);

#endif // INTERACTION_STATE_H
