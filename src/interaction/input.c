#include "input.h"
#include "camera.h"
#include "picking.h"
#include "../graph/graph_actions.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

// Define min/max macros since we're using integers
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// Edge routing mode count (must match renderer.h enum count)
#define EDGE_ROUTING_COUNT 4

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
                         int mods);
static void mouse_button_callback(GLFWwindow *window, int button, int action,
                                  int mods);
static void mouse_callback(GLFWwindow *window, double xpos, double ypos);

void interaction_init(GLFWwindow *window) {
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void interaction_process_continuous_input(AppState *state, float delta_time) {
    GLFWwindow *window = state->window;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // Determine movement speed (Shift increases speed)
    float speed_multiplier = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        speed_multiplier = 3.0f;
    }
    float adjusted_delta = delta_time * speed_multiplier;

    // Handle camera movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera_process_keyboard(&state->camera, CAMERA_DIR_FORWARD, adjusted_delta);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera_process_keyboard(&state->camera, CAMERA_DIR_BACKWARD, adjusted_delta);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera_process_keyboard(&state->camera, CAMERA_DIR_LEFT, adjusted_delta);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera_process_keyboard(&state->camera, CAMERA_DIR_RIGHT, adjusted_delta);
}

GLFWmonitor* interaction_get_current_monitor(GLFWwindow *window) {
    int window_x, window_y;
    glfwGetWindowPos(window, &window_x, &window_y);

    int monitor_count;
    GLFWmonitor **monitors = glfwGetMonitors(&monitor_count);

    GLFWmonitor *best_monitor = glfwGetPrimaryMonitor();
    int best_overlap = 0;

    for (int i = 0; i < monitor_count; i++) {
        GLFWmonitor *monitor = monitors[i];

        int mx, my;
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwGetMonitorPos(monitor, &mx, &my);

        int overlap_x = (min(window_x + 3440, mx + mode->width) -
                         max(window_x, mx));
        int overlap_y = (min(window_y + 1440, my + mode->height) -
                         max(window_y, my));

        int overlap = max(0, overlap_x) * max(0, overlap_y);
        if (overlap > best_overlap) {
            best_overlap = overlap;
            best_monitor = monitor;
        }
    }
    return best_monitor;
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
                         int mods) {
    if (action != GLFW_PRESS)
        return;

    AppState *state = (AppState *)glfwGetWindowUserPointer(window);
    if (!state)
        return;

    switch (key) {
    case GLFW_KEY_T:
        state->renderer.showLabels = !state->renderer.showLabels;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    case GLFW_KEY_N:
        state->renderer.showNodes = !state->renderer.showNodes;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    case GLFW_KEY_P:
        state->renderer.showSpheres = !state->renderer.showSpheres;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    case GLFW_KEY_E:
        state->renderer.showEdges = !state->renderer.showEdges;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    case GLFW_KEY_L:
        graph_action_cycle_layout(state);
        break;
    case GLFW_KEY_Y:
        graph_action_cycle_community_arrangement(state);
        break;
    case GLFW_KEY_I:
        graph_action_run_iteration(state);
        break;
    case GLFW_KEY_C:
        graph_action_cycle_cluster(state);
        break;
    case GLFW_KEY_U:
        graph_action_cycle_centrality(state);
        break;
    case GLFW_KEY_M:
        // Cycle through edge routing modes
        state->renderer.currentRoutingMode = 
            (state->renderer.currentRoutingMode + 1) % EDGE_ROUTING_COUNT;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    case GLFW_KEY_R:
        graph_action_reset(state);
        break;
    case GLFW_KEY_H:
        state->renderer.showUI = !state->renderer.showUI;
        break;
    case GLFW_KEY_1:
    case GLFW_KEY_2:
    case GLFW_KEY_3:
    case GLFW_KEY_4:
    case GLFW_KEY_5:
    case GLFW_KEY_6:
    case GLFW_KEY_7:
    case GLFW_KEY_8:
    case GLFW_KEY_9: {
        int min_deg = key - GLFW_KEY_0;
        if (min_deg > 0)
            graph_action_filter_degree(state, min_deg);
        break;
    }
    case GLFW_KEY_K: {
        int current_k = state->current_graph.props.coreness_filter;
        int new_k = current_k + 1;
        if (new_k > 20)
            new_k = 0;
        state->current_graph.props.coreness_filter = new_k;
        if (new_k > 0)
            graph_action_filter_coreness(state, new_k);
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    }
    case GLFW_KEY_J:
        graph_action_highlight_infrastructure(state);
        break;
    case GLFW_KEY_KP_ADD:
    case GLFW_KEY_EQUAL:
        state->renderer.layoutScale *= 1.2f;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    case GLFW_KEY_KP_SUBTRACT:
    case GLFW_KEY_MINUS:
        state->renderer.layoutScale /= 1.2f;
        renderer_update_graph(&state->renderer, &state->current_graph);
        break;
    }
}

static void mouse_button_callback(GLFWwindow *window, int button, int action,
                                  int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        static double lastClickTime = 0;
        double currentTime = glfwGetTime();
        bool isDoubleClick = (currentTime - lastClickTime) < 0.3;
        lastClickTime = currentTime;

        AppState *state = (AppState *)glfwGetWindowUserPointer(window);
        if (state) {
            interaction_pick_object(state, isDoubleClick);
        }
    }
}

static void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
    AppState *state = (AppState *)glfwGetWindowUserPointer(window);
    if (state) {
        camera_process_mouse(&state->camera, (float)xpos, (float)ypos);
    }
}

