#include "command_registry.h"
#include "graph/wrappers_layout.h"

const CommandDef g_command_registry[] = {
    { "Layout/Force-Directed", "lay_force_fr", "Fruchterman-Reingold", compute_lay_force_fr, apply_layout_matrix, free_layout_matrix }
};

const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
