#include "renderer.h"
#include "graph_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <cglm/cglm.h>

vec3 cameraPos = {0.0f, 0.0f, 50.0f};
vec3 cameraFront = {0.0f, 0.0f, -1.0f};
vec3 cameraUp = {0.0f, 1.0f, 0.0f};
float yaw = -90.0f;
float pitch = 0.0f;
float lastX = 400, lastY = 300;
bool firstMouse = true;

bool isFullscreen = false;
int windowedX, windowedY, windowedWidth, windowedHeight;

GLFWmonitor* get_current_monitor(GLFWwindow* window) {
    int nmonitors;
    GLFWmonitor** monitors = glfwGetMonitors(&nmonitors);
    if (nmonitors <= 1) return glfwGetPrimaryMonitor();
    int wx, wy, ww, wh;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    int best_overlap = 0;
    GLFWmonitor* best_monitor = monitors[0];
    for (int i = 0; i < nmonitors; i++) {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        int mw = mode->width;
        int mh = mode->height;
        int overlap = fmax(0, fmin(wx + ww, mx + mw) - fmax(wx, mx)) * fmax(0, fmin(wy + wh, my + mh) - fmax(wy, my));
        if (overlap > best_overlap) { best_overlap = overlap; best_monitor = monitors[i]; }
    }
    return best_monitor;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        if (!isFullscreen) {
            glfwGetWindowPos(window, &windowedX, &windowedY);
            glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
            GLFWmonitor* monitor = get_current_monitor(window);
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            glfwSetWindowMonitor(window, NULL, windowedX, windowedY, windowedWidth, windowedHeight, 0);
        }
        isFullscreen = !isFullscreen;
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos; 
    lastX = (float)xpos; lastY = (float)ypos;
    float sensitivity = 0.1f;
    xoffset *= sensitivity; yoffset *= sensitivity;
    yaw += xoffset; pitch += yoffset;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    vec3 front;
    front[0] = cosf(glm_rad(yaw)) * cosf(glm_rad(pitch));
    front[1] = sinf(glm_rad(pitch));
    front[2] = sinf(glm_rad(yaw)) * cosf(glm_rad(pitch));
    glm_vec3_normalize_to(front, cameraFront);
}

void processInput(GLFWwindow *window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    float cameraSpeed = 20.0f * deltaTime;
    vec3 temp;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { glm_vec3_scale(cameraFront, cameraSpeed, temp); glm_vec3_add(cameraPos, temp, cameraPos); }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { glm_vec3_scale(cameraFront, cameraSpeed, temp); glm_vec3_sub(cameraPos, temp, cameraPos); }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { vec3 cross; glm_vec3_cross(cameraFront, cameraUp, cross); glm_vec3_normalize(cross); glm_vec3_scale(cross, cameraSpeed, temp); glm_vec3_sub(cameraPos, temp, cameraPos); }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { vec3 cross; glm_vec3_cross(cameraFront, cameraUp, cross); glm_vec3_normalize(cross); glm_vec3_scale(cross, cameraSpeed, temp); glm_vec3_add(cameraPos, temp, cameraPos); }
}

void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] <graph.graphml>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -l, --layout <type>    Layout type: fr (default), kk, random, sphere, grid, umap, drl\n");
    fprintf(stderr, "  -n, --nodes <count>    Limit number of nodes to load\n");
    fprintf(stderr, "  --node-attr <name>     Attribute for node size (default: pagerank)\n");
    fprintf(stderr, "  --edge-attr <name>     Attribute for edge size (default: betweenness)\n");
}

int main(int argc, char** argv) {
    LayoutType layout_type = LAYOUT_FR_3D;
    int node_limit = -1;
    char* node_attr = NULL;
    char* edge_attr = NULL;
    int opt;
    static struct option long_options[] = {
        {"layout", required_argument, 0, 'l'},
        {"nodes", required_argument, 0, 'n'},
        {"node-attr", required_argument, 0, 1},
        {"edge-attr", required_argument, 0, 2},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "l:n:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'l':
                if (strcmp(optarg, "fr") == 0) layout_type = LAYOUT_FR_3D;
                else if (strcmp(optarg, "kk") == 0) layout_type = LAYOUT_KK_3D;
                else if (strcmp(optarg, "random") == 0) layout_type = LAYOUT_RANDOM_3D;
                else if (strcmp(optarg, "sphere") == 0) layout_type = LAYOUT_SPHERE;
                else if (strcmp(optarg, "grid") == 0) layout_type = LAYOUT_GRID_3D;
                else if (strcmp(optarg, "umap") == 0) layout_type = LAYOUT_UMAP_3D;
                else if (strcmp(optarg, "drl") == 0) layout_type = LAYOUT_DRL_3D;
                break;
            case 'n': node_limit = atoi(optarg); break;
            case 1: node_attr = optarg; break;
            case 2: edge_attr = optarg; break;
            default: print_usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind >= argc) { print_usage(argv[0]); return EXIT_FAILURE; }

    GraphData graph;
    if (graph_load_graphml(argv[optind], &graph, layout_type, node_limit, node_attr, edge_attr) != 0) {
        return EXIT_FAILURE;
    }

    if (!glfwInit()) return EXIT_FAILURE;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(3440, 1440, "Graph Sphere", NULL, NULL);
    if (!window) return EXIT_FAILURE;
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    Renderer renderer;
    if (renderer_init(&renderer, window, &graph) != 0) return EXIT_FAILURE;
    graph_free_data(&graph);
    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        glfwPollEvents();
        processInput(window, deltaTime);
        renderer_update_view(&renderer, cameraPos, cameraFront, cameraUp);
        renderer_draw_frame(&renderer);
    }
    renderer_cleanup(&renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
