#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H
#include <igraph.h>

// Forward declare for the apply function (bridge to UI)
struct ExecutionContext;

// 1. Pure math function. Returns allocated result (e.g., igraph_matrix_t*)
typedef void* (*IgraphWorkerFunc)(igraph_t* graph);

// 2. Main thread function to sync result to the visualization state
typedef void (*IgraphApplyFunc)(struct ExecutionContext* ctx, void* result_data);

// 3. Cleanup function to free the result_data
typedef void (*IgraphFreeFunc)(void* result_data);

typedef struct {
    const char* category_path;  // e.g., "Layout/Force-Directed"
    const char* command_id;     // e.g., "lay_force_fr"
    const char* display_name;   // e.g., "Fruchterman-Reingold"
    IgraphWorkerFunc worker_func;
    IgraphApplyFunc apply_func;
    IgraphFreeFunc free_func;
} CommandDef;

extern const CommandDef g_command_registry[];
extern const int g_command_registry_size;

#endif
