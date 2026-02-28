#ifndef INTERACTION_STATE_H
#define INTERACTION_STATE_H

#include <igraph.h>
#include <stdbool.h>

// --- Parameter Types required by igraph functions ---
typedef enum {
    PARAM_TYPE_INT,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_REAL,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_NODE_SELECTION, // Requires 3D raycast picking
    PARAM_TYPE_EDGE_SELECTION, // Requires 3D raycast picking
    PARAM_TYPE_ENUM            // E.g., choosing a layout algorithm
} ParameterType;

// --- Definition of a single input parameter ---
typedef struct {
    const char* name;
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
typedef struct {
    igraph_t* current_graph;
    CommandParameter* params;
    int num_params;
    // Callbacks for UI updates
    void (*update_visuals_callback)(void); 
} ExecutionContext;

// Typedef for the generic wrapper function
typedef void (*IgraphWrapperFunc)(ExecutionContext* ctx);

// --- The Action Node (Leaf in the menu) ---
typedef struct {
    const char* id_name;
    const char* display_name;
    const char* icon_path;     // Path to Vulkan texture
    
    int num_params;
    CommandParameter* params;  // Array of required parameters
    
    IgraphWrapperFunc execute; // The actual C function that calls igraph
} IgraphCommand;

// --- 3D Spherical Menu Tree Structure ---
typedef enum {
    NODE_BRANCH, // Submenu
    NODE_LEAF    // Actionable Command
} MenuNodeType;

typedef struct MenuNode {
    MenuNodeType type;
    const char* label;
    int icon_texture_id;
    
    // 3D Visual State
    float target_phi;   // Spherical coordinate
    float target_theta; // Spherical coordinate
    float current_radius; 
    float target_radius;
    
    // For Branches
    int num_children;
    struct MenuNode** children;
    
    // For Leaves
    IgraphCommand* command;
} MenuNode;

// --- Application State Machine ---
typedef enum {
    STATE_GRAPH_VIEW,         // Freely navigating the graph
    STATE_MENU_OPEN,          // Sphere menu is active, dimming the graph
    STATE_AWAITING_SELECTION, // User must pick nodes/edges with the mouse/laser
    STATE_AWAITING_INPUT,     // Floating 3D form (sliders/text)
    STATE_EXECUTING           // Calculating (blocks input or shows progress bar)
} AppInteractionState;

typedef struct {
    AppInteractionState current_state;
    MenuNode* root_menu;
    MenuNode* active_menu_level;
    
    IgraphCommand* pending_command;
    int selection_step; // How many nodes have we picked so far?
    
    igraph_t* target_graph;
} AppContext;

// Function declarations
void update_app_state(AppContext* app);
void app_context_init(AppContext* ctx, igraph_t* graph, MenuNode* root_menu);
void app_context_destroy(AppContext* ctx);
void handle_menu_selection(AppContext* app, MenuNode* selected_node);
void check_pending_command_requirements(AppContext* app);

#endif // INTERACTION_STATE_H
