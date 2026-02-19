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
float lastX = 1720, lastY = 720;
bool firstMouse = true;

bool isFullscreen = false;
int windowedX, windowedY, windowedWidth, windowedHeight;

Renderer renderer;
GraphData currentGraph;
char* currentFilename;
LayoutType currentLayout;
int currentNodeLimit;
char* currentNodeAttr;
char* currentEdgeAttr;

const char* layout_names[] = { "Fruchterman-Reingold 3D", "Kamada-Kawai 3D", "Random 3D", "Sphere", "Grid 3D", "UMAP 3D", "DrL 3D" };

void update_layout() {
    printf("Switching to layout: %s\n", layout_names[currentLayout]);
    graph_free_data(&currentGraph);
    if (graph_load_graphml(currentFilename, &currentGraph, currentLayout, currentNodeLimit, currentNodeAttr, currentEdgeAttr) == 0) {
        renderer_update_graph(&renderer, &currentGraph);
    }
}

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
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_F:
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
            break;
        case GLFW_KEY_T:
            renderer.showLabels = !renderer.showLabels;
            printf("Labels: %s\n", renderer.showLabels ? "ON" : "OFF");
            break;
        case GLFW_KEY_L:
            currentLayout = (currentLayout + 1) % 7;
            update_layout();
            break;
        case GLFW_KEY_KP_ADD:
        case GLFW_KEY_EQUAL:
            renderer.layoutScale *= 1.2f;
            renderer_update_graph(&renderer, &currentGraph);
            break;
        case GLFW_KEY_KP_SUBTRACT:
        case GLFW_KEY_MINUS:
            renderer.layoutScale /= 1.2f;
            renderer_update_graph(&renderer, &currentGraph);
            break;
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
    currentLayout = LAYOUT_FR_3D;
    currentNodeLimit = -1;
    currentNodeAttr = NULL;
    currentEdgeAttr = NULL;
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
                if (strcmp(optarg, "fr") == 0) currentLayout = LAYOUT_FR_3D;
                else if (strcmp(optarg, "kk") == 0) currentLayout = LAYOUT_KK_3D;
                else if (strcmp(optarg, "random") == 0) currentLayout = LAYOUT_RANDOM_3D;
                else if (strcmp(optarg, "sphere") == 0) currentLayout = LAYOUT_SPHERE;
                else if (strcmp(optarg, "grid") == 0) currentLayout = LAYOUT_GRID_3D;
                else if (strcmp(optarg, "umap") == 0) currentLayout = LAYOUT_UMAP_3D;
                else if (strcmp(optarg, "drl") == 0) currentLayout = LAYOUT_DRL_3D;
                break;
            case 'n': currentNodeLimit = atoi(optarg); break;
            case 1: currentNodeAttr = optarg; break;
            case 2: currentEdgeAttr = optarg; break;
            default: print_usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind >= argc) { print_usage(argv[0]); return EXIT_FAILURE; }
    currentFilename = argv[optind];

    if (graph_load_graphml(currentFilename, &currentGraph, currentLayout, currentNodeLimit, currentNodeAttr, currentEdgeAttr) != 0) {
        return EXIT_FAILURE;
    }

    if (!glfwInit()) return EXIT_FAILURE;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(3440, 1440, "Graph Sphere", NULL, NULL);
    if (!window) return EXIT_FAILURE;
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (renderer_init(&renderer, window, &currentGraph) != 0) return EXIT_FAILURE;
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
    graph_free_data(&currentGraph);
    renderer_cleanup(&renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
