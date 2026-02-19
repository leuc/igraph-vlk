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
LayoutType currentLayout = LAYOUT_FR_3D;
ClusterType currentCluster = CLUSTER_FASTGREEDY;
CentralityType currentCentrality = CENTRALITY_PAGERANK;
int currentNodeLimit;
char* currentNodeAttr;
char* currentEdgeAttr;

const char* layout_names[] = { "FR", "KK", "Random", "Sphere", "Grid", "UMAP", "DrL" };
const char* cluster_names[] = { "FastGreedy", "Walktrap", "LabelProp", "Multilevel", "Leiden" };
const char* centrality_names[] = { "PageRank", "Hub", "Auth", "Betweenness", "Degree", "Closeness", "Harmonic", "Eigen", "Strength", "Constraint" };

void update_ui_text(float fps) {
    char buf[512];
    snprintf(buf, sizeof(buf), 
        "[L]ayout: %s | [G]roup: %s | [C]entrality: %s | [T]ext: %s | FPS: %.1f",
        layout_names[currentLayout],
        cluster_names[currentCluster],
        centrality_names[currentCentrality],
        renderer.showLabels ? "ON" : "OFF",
        fps
    );
    renderer_update_ui(&renderer, buf);
}

void update_layout() {
    graph_free_data(&currentGraph);
    if (graph_load_graphml(currentFilename, &currentGraph, currentLayout, currentNodeLimit, currentNodeAttr, currentEdgeAttr) == 0) {
        renderer_update_graph(&renderer, &currentGraph);
    }
}

void run_clustering() {
    graph_cluster(currentFilename, &currentGraph, currentCluster, currentNodeLimit);
    renderer_update_graph(&renderer, &currentGraph);
}

void run_centrality() {
    graph_calculate_centrality(currentFilename, &currentGraph, currentCentrality, currentNodeLimit);
    renderer_update_graph(&renderer, &currentGraph);
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
        case GLFW_KEY_T: renderer.showLabels = !renderer.showLabels; break;
        case GLFW_KEY_L: currentLayout = (currentLayout + 1) % 7; update_layout(); break;
        case GLFW_KEY_G: currentCluster = (currentCluster + 1) % CLUSTER_COUNT; run_clustering(); break;
        case GLFW_KEY_C: currentCentrality = (currentCentrality + 1) % CENTRALITY_COUNT; run_centrality(); break;
        case GLFW_KEY_KP_ADD:
        case GLFW_KEY_EQUAL: renderer.layoutScale *= 1.2f; renderer_update_graph(&renderer, &currentGraph); break;
        case GLFW_KEY_KP_SUBTRACT:
        case GLFW_KEY_MINUS: renderer.layoutScale /= 1.2f; renderer_update_graph(&renderer, &currentGraph); break;
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

int main(int argc, char** argv) {
    int opt;
    static struct option long_options[] = { {"layout", 1, 0, 'l'}, {"nodes", 1, 0, 'n'}, {"node-attr", 1, 0, 1}, {"edge-attr", 1, 0, 2}, {0, 0, 0, 0} };
    while ((opt = getopt_long(argc, argv, "l:n:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'l': if (strcmp(optarg, "fr") == 0) currentLayout = LAYOUT_FR_3D; else if (strcmp(optarg, "kk") == 0) currentLayout = LAYOUT_KK_3D; break;
            case 'n': currentNodeLimit = atoi(optarg); break;
            case 1: currentNodeAttr = optarg; break;
            case 2: currentEdgeAttr = optarg; break;
        }
    }
    if (optind >= argc) return EXIT_FAILURE;
    currentFilename = argv[optind];
    if (graph_load_graphml(currentFilename, &currentGraph, currentLayout, currentNodeLimit, currentNodeAttr, currentEdgeAttr) != 0) return EXIT_FAILURE;
    if (!glfwInit()) return EXIT_FAILURE;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(3440, 1440, "Graph Sphere", NULL, NULL);
    if (!window) return EXIT_FAILURE;
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (renderer_init(&renderer, window, &currentGraph) != 0) return EXIT_FAILURE;
    float lastFrame = 0.0f;
    float fpsTimer = 0.0f;
    int frameCount = 0;
    float currentFps = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 1.0f) { currentFps = frameCount / fpsTimer; frameCount = 0; fpsTimer = 0.0f; }
        glfwPollEvents();
        processInput(window, deltaTime);
        update_ui_text(currentFps);
        renderer_update_view(&renderer, cameraPos, cameraFront, cameraUp);
        renderer_draw_frame(&renderer);
    }
    graph_free_data(&currentGraph);
    renderer_cleanup(&renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
