#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkan/renderer.h"
#include "vulkan/animation_manager.h"
#include "graph/graph_core.h"
#include "graph/graph_types.h"
#include "interaction/camera.h"

/**
 * Central application state that glues together all modules.
 * This replaces all global variables previously in main.c.
 */
typedef struct {
    /* Core Subsystems */
    Renderer renderer;
    GraphData current_graph;
    AnimationManager anim_manager;
    Camera camera;

    /* Window / System State */
    GLFWwindow *window;
    bool is_fullscreen;
    int win_x, win_y, win_w, win_h;

    /* Application Logic State */
    char *current_filename;
    LayoutType current_layout;
    ClusterType current_cluster;
    CentralityType current_centrality;
    CommunityArrangementMode current_comm_arrangement;
    char *node_attr;
    char *edge_attr;

    /* Interaction State */
    int last_picked_node;
    int last_picked_edge;

    /* Timing */
    float last_frame_time;
    float fps_timer;
    int frame_count;
    float current_fps;
} AppState;

