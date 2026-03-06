#ifndef INTERACTION_STATE_H
#define INTERACTION_STATE_H

#include "interaction/spatial.h"
#include <cglm/cglm.h>
#include <igraph.h>
#include <stdbool.h>

// Forward declaration to avoid circular dependency with command_registry.h
struct CommandDef;

// --- Parameter Types required by igraph functions ---
typedef enum {
	PARAM_TYPE_INT,
	PARAM_TYPE_FLOAT,
	PARAM_TYPE_REAL,
	PARAM_TYPE_BOOL,
	PARAM_TYPE_NODE_SELECTION, // Requires 3D raycast picking
	PARAM_TYPE_EDGE_SELECTION, // Requires 3D raycast picking
	PARAM_TYPE_ENUM			   // E.g., choosing a layout algorithm
} ParameterType;

// --- Definition of a single input parameter ---
typedef struct
{
	const char *name;
	ParameterType type;
	union {
		int i_val;
		igraph_real_t f_val;
		bool b_val;
		igraph_integer_t selection_id; // Stores the picked igraph_integer_t vertex/edge ID
	} value;
	// Limits for UI sliders/forms
	igraph_real_t min_val;
	igraph_real_t max_val;
} CommandParameter;

// --- Generic Command Execution Context ---
typedef struct
{
	igraph_t *current_graph;
	CommandParameter *params;
	int num_params;
	// Callbacks for UI updates
	void (*update_visuals_callback)(void);
	// Pointer to full application state for layout updates
	struct AppState *app_state;
} ExecutionContext;

// Typedef for the generic wrapper function
typedef void (*IgraphWrapperFunc)(ExecutionContext *ctx);

// --- The Action Node (Leaf in the menu) ---
typedef struct
{
	const char *id_name;
	const char *display_name;
	const char *icon_path; // Path to Vulkan texture

	int num_params;
	CommandParameter *params; // Array of required parameters

	// Old execution method
	IgraphWrapperFunc execute; // The actual C function that calls igraph

	// New data-driven method
	const struct CommandDef *cmd_def; // Pointer to command definition in registry

	bool produces_visual_output; // If true, shows results overlay before returning to graph
} IgraphCommand;

// --- 3D Spherical Menu Tree Structure ---
typedef enum {
	NODE_BRANCH,	   // Opens a submenu card
	NODE_LEAF_COMMAND, // Standard clickable action button
	NODE_INPUT_TEXT,   // Text input field
	NODE_INPUT_TOGGLE  // Checkbox/boolean toggle
} MenuNodeType;

typedef struct MenuNode
{
	MenuNodeType type;
	const char *label;
	int icon_texture_id;

	// 3D Visual State
	float target_phi;	// Spherical coordinate
	float target_theta; // Spherical coordinate
	float current_radius;
	float target_radius;

	// Cached spatial data (computed once during layout, reused for rendering/picking)
	vec3 text_anchor_pos; // The 3D position where left-aligned text starts
	vec3 quad_center_pos; // The 3D position of the background quad's exact center
	vec3 world_pos;		  // Cached world position (same as quad_center_pos)
	versor rotation;	  // Cached rotation quaternion for the quad
	vec3 right_vec;		  // Orthonormal right vector for the billboard
	vec3 up_vec;		  // Orthonormal up vector for the billboard
	float box_width;	  // The unscaled width of the menu node (at 100%)
	float box_height;	  // The unscaled height of the menu node (at 100%)

	// NeXTSTEP Card Dimensions
	float card_width;  // Width of the entire card background
	float card_height; // Height of the entire card background
	vec3 card_bg_pos;  // Position of the card background quad center

	// Input Handling
	bool is_focused;		// Tracks keyboard focus for input fields
	char input_buffer[256]; // User-typed text for input fields
	bool toggle_state;		// Boolean state for toggle inputs

	// For Branches
	int num_children;
	struct MenuNode **children;

	// For Leaves
	IgraphCommand *command;

	bool hovered;	  // For visual feedback
	bool is_expanded; // Whether submenu is unfolded
} MenuNode;

// --- Floating 3D Slider/Dial Widget for Numeric Input ---
typedef struct
{
	bool active;
	ParameterType param_type; // PARAM_TYPE_FLOAT or PARAM_TYPE_INT

	// Current value being adjusted
	union {
		float float_val;
		int int_val;
	} current;

	// Constraints
	union {
		struct
		{
			float min_val;
			float max_val;
			float step;
		} float_range;
		struct
		{
			int min_val;
			int max_val;
			int step;
		} int_range;
	} constraints;

	// 3D position (world space)
	vec3 world_position;

	// Visual parameters
	float slider_length; // Length of the slider track
	float slider_radius; // Radius of the draggable knob
	char *label;		 // e.g., "Probability: 0.45"
} NumericInputWidget;

// --- Generic Info Card Data ---
typedef struct
{
	char key[32];
	char value[64];
} InfoKeyValuePair;

// Data passed from the worker thread back to main thread
typedef struct
{
	char title[64];
	int num_pairs;
	InfoKeyValuePair pairs[8];
} InfoCardData;

// State held by the UI
typedef struct
{
	bool is_visible;
	char title[64];
	int num_pairs;
	InfoKeyValuePair pairs[8];
} InfoCardState;

// --- Application State Machine ---
typedef enum {
	STATE_GRAPH_VIEW,		  // Freely navigating the graph
	STATE_MENU_OPEN,		  // Sphere menu is active, dimming the graph
	STATE_AWAITING_SELECTION, // User must pick nodes/edges with the mouse/laser
	STATE_AWAITING_INPUT,	  // Floating 3D form (sliders/text)
	STATE_EXECUTING,		  // Calculating (blocks input or shows progress bar)
	STATE_JOB_IN_PROGRESS,	  // Long-running operation in worker thread
	STATE_DISPLAY_RESULTS	  // Showing execution results (overlays, histograms, etc.)
} AppInteractionState;

typedef struct AppContext
{
	AppInteractionState current_state;
	MenuNode *root_menu;
	MenuNode *active_menu_level;

	// Crosshair hover tracking for immersive menu selection
	MenuNode *crosshair_hovered_node;

	IgraphCommand *pending_command;
	int selection_step; // How many nodes have we picked so far?

	igraph_t *target_graph;

	// Results display state
	bool has_visual_results;
	void *results_data; // Optional: pointer to results for overlay rendering

	// Numeric input widget state
	NumericInputWidget numeric_input;

	// Info card state (generic inspector panel)
	InfoCardState info_card;

	// Menu world-space anchor (captured when menu opens)
	SpatialBasis menu_spawn_basis;
} AppContext;

// Function declarations
typedef struct AppState AppState;
void update_app_state(AppState *state);
void app_context_init(AppContext *ctx, igraph_t *graph, MenuNode *root_menu);
void app_context_destroy(AppContext *ctx);
void handle_menu_selection(AppContext *app, MenuNode *selected_node);
void check_pending_command_requirements(AppContext *app);

#endif // INTERACTION_STATE_H
