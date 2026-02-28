#include "app_state.h"
#include "interaction/input.h"
#include "interaction/camera.h"
#include "ui/hud.h"
#include "graph/graph_actions.h"
#include "graph/graph_io.h"
#include "vulkan/renderer.h"
#include "vulkan/animation_manager.h"
#include "interaction/state.h"
#include "ui/sphere_menu.h"
#include <GLFW/glfw3.h>


#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * igraph-vlk: Vulkan-based 3D graph visualization
 *
 * A modular refactoring of main.c that uses the AppState pattern
 * to cleanly separate concerns between modules.
 */

int main(int argc, char **argv) {
    // Parse command line arguments
    int opt;
    static struct option long_options[] = {{"layout", 1, 0, 'l'},
                                           {"node-attr", 1, 0, 1},
                                           {"edge-attr", 1, 0, 2},
                                           {0, 0, 0, 0}};

    AppState app = {0};

    // Set defaults
    app.current_layout = LAYOUT_OPENORD_3D;
    app.current_cluster = CLUSTER_FASTGREEDY;
    app.current_centrality = CENTRALITY_PAGERANK;
    app.current_comm_arrangement = COMMUNITY_ARRANGEMENT_NONE;
    app.last_picked_node = -1;
    app.last_picked_edge = -1;
    app.win_w = 3440;
    app.win_h = 1440;

    while ((opt = getopt_long(argc, argv, "l:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'l':
            if (strcmp(optarg, "fr") == 0)
                app.current_layout = LAYOUT_FR_3D;
            else if (strcmp(optarg, "kk") == 0)
                app.current_layout = LAYOUT_KK_3D;
            else if (strcmp(optarg, "umap") == 0)
                app.current_layout = LAYOUT_UMAP_3D;
            break;
        case 1:
            app.node_attr = optarg;
            break;
        case 2:
            app.edge_attr = optarg;
            break;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s [--layout <fr|kk|umap>] [--node-attr <attr>] "
                        "[--edge-attr <attr>] <graph.graphml>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    app.current_filename = argv[optind];

    // Initialize graph data
    app.current_graph.graph_initialized = false;
    if (graph_load_graphml(app.current_filename, &app.current_graph,
                           app.current_layout, app.node_attr,
                           app.edge_attr) != 0) {
        fprintf(stderr, "Failed to load graph: %s\n", app.current_filename);
        return EXIT_FAILURE;
    }

    // Initialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Create window
    app.window = glfwCreateWindow(app.win_w, app.win_h, "Graph Sphere", NULL, NULL);
    if (!app.window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Store user pointer for callbacks
    glfwSetWindowUserPointer(app.window, &app);

    // Initialize input handling (registers GLFW callbacks)
    interaction_init(app.window);

    // Initialize renderer
    if (renderer_init(&app.renderer, app.window, &app.current_graph) != 0) {
        fprintf(stderr, "Failed to initialize renderer\n");
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Initialize animation manager
    animation_manager_init(&app.anim_manager, &app.renderer, &app.current_graph);

    // Initialize camera
    camera_init(&app.camera);

    // Initialize UI/HUD
    ui_hud_init();

    // Initialize FSM menu system
    MenuNode* root_menu = (MenuNode*)malloc(sizeof(MenuNode));
    init_menu_tree(root_menu);
    app_context_init(&app.app_ctx, &app.current_graph.g, root_menu);

    // Initialize timing
    float lastFrame = 0.0f;
    float fpsTimer = 0.0f;
    int frameCount = 0;
    float currentFps = 0.0f;

    // Main loop
    while (!glfwWindowShouldClose(app.window)) {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // FPS calculation
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 1.0f) {
            currentFps = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // Process input (WASD movement)
        interaction_process_continuous_input(&app, deltaTime);

        // Update HUD text
        ui_hud_update(&app, currentFps);

        // Update App FSM and Menu animations
        update_app_state(&app.app_ctx);
        update_menu_animation(app.app_ctx.root_menu, deltaTime);

        // Update animations
        animation_manager_update(&app.anim_manager, deltaTime);
        if (app.anim_manager.num_animations > 0) {
            renderer_update_graph(&app.renderer, &app.current_graph);
        }

        // Step background layout (OpenOrd / Layered Sphere)
        graph_action_step_background_layout(&app);

        // Update view matrix and draw
        renderer_update_view(&app.renderer, app.camera.pos, app.camera.front,
                             app.camera.up);
        renderer_draw_frame(&app.renderer);

        glfwPollEvents();
    }

    // Cleanup
    app_context_destroy(&app.app_ctx);
    destroy_menu_tree(root_menu);
    graph_free_data(&app.current_graph);
    animation_manager_cleanup(&app.anim_manager);
    renderer_cleanup(&app.renderer);
    glfwDestroyWindow(app.window);
    glfwTerminate();

    return 0;
}

