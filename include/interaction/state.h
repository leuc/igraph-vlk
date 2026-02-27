#ifndef STATE_H
#define STATE_H

#include <igraph.h>
#include <stdbool.h>
#include <stdint.h>  // For fixed-width types if needed

// --- Parameter Types required by igraph functions ---
typedef enum {
    PARAM_TYPE_INT,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_NODE_SELECTION, // Requires 3D raycast picking
    PARAM_TYPE_EDGE_SELECTION, // Requires 3D raycast picking
    PARAM_TYPE_ENUM,           // E.g., choosing a layout algorithm
    PARAM_TYPE_REAL,           // For igraph_real_t (double)
} ParameterType;

// --- Definition of a single input parameter ---
typedef struct {
    const char* name;
    ParameterType type;
    union {
        igraph_integer_t i_val;
        igraph_real_t f_val;     // Using igraph_real_t (double) instead of float
        bool b_val;
        igraph_integer_t selection_id; // Stores the picked igraph_integer_t vertex/edge ID
        igraph_integer_t enum_val;
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

typedef struct MenuNode MenuNode;  // Forward declaration

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
    
    igraph_t* current_graph;  // Pointer to the main graph (changed from target_graph for clarity)
    igraph_t target_graph;    // Temporary graph for computations
} AppContext;

#endif // STATE_H
