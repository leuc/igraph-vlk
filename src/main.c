#include <cglm/cglm.h>
#include <float.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "animation_manager.h" // New include
#include "graph_loader.h"
#include "layered_sphere.h"
#include "layout_openord.h"
#include "renderer.h"

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
char *currentFilename;
LayoutType currentLayout = LAYOUT_OPENORD_3D;
ClusterType currentCluster = CLUSTER_FASTGREEDY;
CentralityType currentCentrality = CENTRALITY_PAGERANK;
char *currentNodeAttr;
char *currentEdgeAttr;

AnimationManager globalAnimationManager; // Declare global animation manager
int last_picked_node = -1; // Stores the index of the last picked node
int last_picked_edge = -1; // Stores the index of the last picked edge

const char *layout_names[] = {"Fruchterman-Reingold",
							  "Kamada-Kawai",
							  "Random",
							  "Sphere",
							  "Grid",
							  "UMAP",
							  "DrL",
							  "OpenOrd",
							  "Layered Sphere"};
const char *cluster_names[] = {"Fast Greedy", "Walktrap", "Label Propagation",
							   "Multilevel", "Leiden"};
const char *centrality_names[] = {
	"PageRank",	 "Hubs",	 "Authorities", "Betweenness", "Degree",
	"Closeness", "Harmonic", "Eigenvector", "Strength",	   "Constraint"};

void update_ui_text(float fps) {
	char stage_info[64] = "";
	if (currentLayout == LAYOUT_OPENORD_3D && currentGraph.openord) {
		snprintf(stage_info, sizeof(stage_info), " [%s:%d]",
				 openord_get_stage_name(currentGraph.openord->stage_id),
				 currentGraph.openord->current_iter);
	} else if (currentLayout == LAYOUT_LAYERED_SPHERE &&
			   currentGraph.layered_sphere) {
		snprintf(stage_info, sizeof(stage_info), " [%s:%d]",
				 layered_sphere_get_stage_name(currentGraph.layered_sphere),
				 currentGraph.layered_sphere->current_iter);
	}

	char buf[1024];
	snprintf(buf, sizeof(buf),
			 "[L]ayout:%s%s [I]terate [C]ommunity:%s Str[u]cture:%s [O]verlap "
			 "[B]ridge [T]ext:%s [N]ode:%d [E]dge:%d Filter:1-9 [K]Core:%d "
			 "[R]eset [H]ide FPS:%.1f",
			 layout_names[currentLayout], stage_info,
			 cluster_names[currentCluster], centrality_names[currentCentrality],
			 renderer.showLabels ? "ON" : "OFF", currentGraph.props.node_count,
			 currentGraph.props.edge_count, currentGraph.props.coreness_filter,
			 fps);
	renderer_update_ui(&renderer, buf);
}

void update_layout() {
	graph_layout_step(&currentGraph, currentLayout, 50);
	renderer_update_graph(&renderer, &currentGraph);
}
void run_clustering() {
	graph_cluster(&currentGraph, currentCluster);
	renderer_update_graph(&renderer, &currentGraph);
}
void run_centrality() {
	graph_calculate_centrality(&currentGraph, currentCentrality);
	renderer_update_graph(&renderer, &currentGraph);
}
void run_iteration() {
	graph_layout_step(&currentGraph, currentLayout, 1);
	renderer_update_graph(&renderer, &currentGraph);
}
void run_filter(int min_deg) {
	graph_filter_degree(&currentGraph, min_deg);
	renderer_update_graph(&renderer, &currentGraph);
}
void run_coreness_filter(int min_core) {
	graph_filter_coreness(&currentGraph, min_core);
	renderer_update_graph(&renderer, &currentGraph);
}
void run_infrastructure() {
	graph_highlight_infrastructure(&currentGraph);
	renderer_update_graph(&renderer, &currentGraph);
}

void run_reset() {
	graph_free_data(&currentGraph);
	currentLayout = LAYOUT_OPENORD_3D;
	renderer.layoutScale = 1.0f;
	currentGraph.props.coreness_filter = 0;
	if (graph_load_graphml(currentFilename, &currentGraph, currentLayout,
						   currentNodeAttr, currentEdgeAttr) == 0) {
		renderer_update_graph(&renderer, &currentGraph);
	}
}

GLFWmonitor *get_current_monitor(GLFWwindow *window) {
	int nmonitors;
	GLFWmonitor **monitors = glfwGetMonitors(&nmonitors);
	if (nmonitors <= 1)
		return glfwGetPrimaryMonitor();
	int wx, wy, ww, wh;
	glfwGetWindowPos(window, &wx, &wy);
	glfwGetWindowSize(window, &ww, &wh);
	int best_overlap = 0;
	GLFWmonitor *best_monitor = monitors[0];
	for (int i = 0; i < nmonitors; i++) {
		int mx, my;
		glfwGetMonitorPos(monitors[i], &mx, &my);
		const GLFWvidmode *mode = glfwGetVideoMode(monitors[i]);
		int overlap = fmax(0, fmin(wx + ww, mx + mode->width) - fmax(wx, mx)) *
					  fmax(0, fmin(wy + wh, my + mode->height) - fmax(wy, my));
		if (overlap > best_overlap) {
			best_overlap = overlap;
			best_monitor = monitors[i];
		}
	}
	return best_monitor;
}

void key_callback(GLFWwindow *window, int key, int scancode, int action,
				  int mods) {
	if (action != GLFW_PRESS)
		return;
	if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
		run_filter(key - GLFW_KEY_0);
		return;
	}
	switch (key) {
	case GLFW_KEY_F:
		if (!isFullscreen) {
			glfwGetWindowPos(window, &windowedX, &windowedY);
			glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
			GLFWmonitor *monitor = get_current_monitor(window);
			const GLFWvidmode *mode = glfwGetVideoMode(monitor);
			glfwSetWindowMonitor(window, monitor, 0, 0, mode->width,
								 mode->height, mode->refreshRate);
		} else {
			glfwSetWindowMonitor(window, NULL, windowedX, windowedY,
								 windowedWidth, windowedHeight, 0);
		}
		isFullscreen = !isFullscreen;
		break;
	case GLFW_KEY_T:
		renderer.showLabels = !renderer.showLabels;
		break;
	case GLFW_KEY_P:
		renderer.showSpheres = !renderer.showSpheres;
		break;
	case GLFW_KEY_N:
		renderer.showNodes = !renderer.showNodes;
		break;
	case GLFW_KEY_E:
		renderer.showEdges = !renderer.showEdges;
		break;
	case GLFW_KEY_H:
		renderer.showUI = !renderer.showUI;
		break;
	case GLFW_KEY_B:
		run_infrastructure();
		break;
	case GLFW_KEY_K:
		run_coreness_filter(currentGraph.props.coreness_filter + 1);
		break;
	case GLFW_KEY_L:
		currentLayout = (currentLayout + 1) % LAYOUT_COUNT;
		update_layout();
		break;
	case GLFW_KEY_C:
		currentCluster = (currentCluster + 1) % CLUSTER_COUNT;
		run_clustering();
		break;
	case GLFW_KEY_U:
		currentCentrality = (currentCentrality + 1) % CENTRALITY_COUNT;
		run_centrality();
		break;
	case GLFW_KEY_I:
		run_iteration();
		break;
	case GLFW_KEY_O:
		graph_remove_overlaps(&currentGraph, renderer.layoutScale);
		renderer_update_graph(&renderer, &currentGraph);
		break;
	case GLFW_KEY_R:
		run_reset();
		break;

	// --- NEW ROUTING HOT-SWAP ---
	case GLFW_KEY_M:
		renderer.currentRoutingMode = (renderer.currentRoutingMode + 1) % 3;
		printf("Switched Edge Routing Mode to %d\n",
			   renderer.currentRoutingMode);

		if (renderer.currentRoutingMode == ROUTING_MODE_3D_HUB_SPOKE) {
			printf("Generating K-Means Hubs...\n");
			graph_generate_hubs(&currentGraph,
								150); // Generate 150 transit hubs
		}

		renderer_update_graph(&renderer,
							  &currentGraph); // Re-dispatch compute shaders
		break;
		// ----------------------------

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

bool ray_sphere_intersection(vec3 ori, vec3 dir, vec3 center, float radius,
							 float *t) {
	vec3 oc;
	glm_vec3_sub(ori, center, oc);
	float b = glm_vec3_dot(oc, dir);
	float c = glm_vec3_dot(oc, oc) - radius * radius;
	float h = b * b - c;
	if (h < 0.0f)
		return false;
	h = sqrtf(h);
	*t = -b - h;
	return true;
}

float dist_ray_segment(vec3 ori, vec3 dir, vec3 p1, vec3 p2, float *t_out) {
	vec3 u, v, w;
	glm_vec3_sub(p2, p1, u);
	glm_vec3_copy(dir, v);
	glm_vec3_sub(p1, ori, w);
	float a = glm_vec3_dot(u, u);
	float b = glm_vec3_dot(u, v);
	float c = glm_vec3_dot(v, v);
	float d = glm_vec3_dot(u, w);
	float e = glm_vec3_dot(v, w);
	float D = a * c - b * b;
	float sc, tc;
	if (D < 0.000001f) {
		sc = 0.0f;
		tc = (b > c ? d / b : e / c);
	} else {
		sc = (b * e - c * d) / D;
		tc = (a * e - b * d) / D;
	}
	if (sc < 0.0f)
		sc = 0.0f;
	else if (sc > 1.0f)
		sc = 1.0f;
	vec3 p_seg, p_ray;
	glm_vec3_scale(u, sc, p_seg);
	glm_vec3_add(p1, p_seg, p_seg);
	glm_vec3_scale(v, tc, p_ray);
	glm_vec3_add(ori, p_ray, p_ray);
	*t_out = tc;
	return glm_vec3_distance(p_seg, p_ray);
}

void pick_object(bool isDoubleClick) {
	float min_t = FLT_MAX;
	int hit_node = -1;
	int hit_edge = -1;

	// Clear previous selection
	for (uint32_t i = 0; i < currentGraph.node_count; i++)
		currentGraph.nodes[i].selected = 0.0f;
	for (uint32_t i = 0; i < currentGraph.edge_count; i++)
		currentGraph.edges[i].selected = 0.0f;

	// Clear previous animations if a new object is picked or on double-click
	if (isDoubleClick ||
		(last_picked_node != -1 && hit_node != last_picked_node) ||
		(last_picked_edge != -1 && hit_edge != last_picked_edge)) {
		animation_manager_cleanup(&globalAnimationManager);
		animation_manager_init(&globalAnimationManager, &renderer,
							   &currentGraph);
	}

	for (uint32_t i = 0; i < currentGraph.node_count; i++) {
		vec3 pos;
		glm_vec3_scale(currentGraph.nodes[i].position, renderer.layoutScale,
					   pos);
		float radius = currentGraph.nodes[i].size * 0.5f;
		float t;
		if (ray_sphere_intersection(cameraPos, cameraFront, pos, radius, &t)) {
			if (t > 0 && t < min_t) {
				min_t = t;
				hit_node = i;
				hit_edge = -1;
			}
		}
	}
	for (uint32_t i = 0; i < currentGraph.edge_count; i++) {
		vec3 p1, p2;
		glm_vec3_scale(currentGraph.nodes[currentGraph.edges[i].from].position,
					   renderer.layoutScale, p1);
		glm_vec3_scale(currentGraph.nodes[currentGraph.edges[i].to].position,
					   renderer.layoutScale, p2);
		float t, dist;
		dist = dist_ray_segment(cameraPos, cameraFront, p1, p2, &t);
		if (dist < 0.2f && t > 0 && t < min_t) {
			min_t = t;
			hit_node = -1;
			hit_edge = i;
		}
	}
	if (hit_node != -1) {
		currentGraph.nodes[hit_node].selected = 1.0f;
		printf("%s Clicked Node %d: %s\n", isDoubleClick ? "Double" : "Single",
			   hit_node,
			   currentGraph.nodes[hit_node].label
				   ? currentGraph.nodes[hit_node].label
				   : "no label");
		last_picked_node = hit_node;

		if (isDoubleClick) {
			printf("Double-clicked node %d. Animating connected edges.\n",
				   hit_node);
			for (uint32_t i = 0; i < currentGraph.edge_count; ++i) {
				uint32_t edge_from = currentGraph.edges[i].from;
				uint32_t edge_to = currentGraph.edges[i].to;
				int direction = 0;

				if (edge_from == hit_node) {
					direction = 1; // From hit_node to edge.to
				} else if (edge_to == hit_node) {
					direction =
						-1; // From hit_node to edge.from (i.e. backward)
				}

				if (direction != 0) {
					animation_manager_toggle_edge(&globalAnimationManager, i,
												  direction);
					printf("From: %d To: %d \n", edge_from, edge_to);
				}
			}
		}
	} else if (hit_edge != -1) {
		currentGraph.edges[hit_edge].selected = 1.0f;
		printf("%s Clicked Edge %d: %d -> %d\n",
			   isDoubleClick ? "Double" : "Single", hit_edge,
			   currentGraph.edges[hit_edge].from,
			   currentGraph.edges[hit_edge].to);
		last_picked_node = -1; // Clear node selection
	} else {
		last_picked_node = -1;
	}

	renderer_update_graph(&renderer, &currentGraph);
}

void mouse_button_callback(GLFWwindow *window, int button, int action,
						   int mods) {
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		static double lastClickTime = 0;
		double currentTime = glfwGetTime();
		bool isDoubleClick = (currentTime - lastClickTime) < 0.3;
		lastClickTime = currentTime;
		pick_object(isDoubleClick);
	}
}

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
	if (firstMouse) {
		lastX = (float)xpos;
		lastY = (float)ypos;
		firstMouse = false;
	}
	float xoffset = ((float)xpos - lastX) * 0.1f;
	float yoffset = (lastY - (float)ypos) * 0.1f;
	lastX = (float)xpos;
	lastY = (float)ypos;
	yaw += xoffset;
	pitch += yoffset;
	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;
	vec3 front;
	front[0] = cosf(glm_rad(yaw)) * cosf(glm_rad(pitch));
	front[1] = sinf(glm_rad(pitch));
	front[2] = sinf(glm_rad(yaw)) * cosf(glm_rad(pitch));
	glm_vec3_normalize_to(front, cameraFront);
}

void processInput(GLFWwindow *window, float deltaTime) {
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
		glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
	float cameraSpeed = 20.0f * deltaTime;
	vec3 temp;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		glm_vec3_scale(cameraFront, cameraSpeed, temp);
		glm_vec3_add(cameraPos, temp, cameraPos);
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		glm_vec3_scale(cameraFront, cameraSpeed, temp);
		glm_vec3_sub(cameraPos, temp, cameraPos);
	}
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
		vec3 cross;
		glm_vec3_cross(cameraFront, cameraUp, cross);
		glm_vec3_normalize(cross);
		glm_vec3_scale(cross, cameraSpeed, temp);
		glm_vec3_sub(cameraPos, temp, cameraPos);
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
		vec3 cross;
		glm_vec3_cross(cameraFront, cameraUp, cross);
		glm_vec3_normalize(cross);
		glm_vec3_scale(cross, cameraSpeed, temp);
		glm_vec3_add(cameraPos, temp, cameraPos);
	}
}

int main(int argc, char **argv) {
	int opt;
	static struct option long_options[] = {{"layout", 1, 0, 'l'},
										   {"node-attr", 1, 0, 1},
										   {"edge-attr", 1, 0, 2},
										   {0, 0, 0, 0}};
	while ((opt = getopt_long(argc, argv, "l:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'l':
			if (strcmp(optarg, "fr") == 0)
				currentLayout = LAYOUT_FR_3D;
			else if (strcmp(optarg, "kk") == 0)
				currentLayout = LAYOUT_KK_3D;
			break;
		case 1:
			currentNodeAttr = optarg;
			break;
		case 2:
			currentEdgeAttr = optarg;
			break;
		}
	}
	if (optind >= argc)
		return EXIT_FAILURE;
	currentFilename = argv[optind];
	currentGraph.graph_initialized = false;
	if (graph_load_graphml(currentFilename, &currentGraph, currentLayout,
						   currentNodeAttr, currentEdgeAttr) != 0)
		return EXIT_FAILURE;
	if (!glfwInit())
		return EXIT_FAILURE;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window =
		glfwCreateWindow(3440, 1440, "Graph Sphere", NULL, NULL);
	if (!window)
		return EXIT_FAILURE;
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	if (renderer_init(&renderer, window, &currentGraph) != 0)
		return EXIT_FAILURE;

	animation_manager_init(&globalAnimationManager, &renderer, &currentGraph);

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
		if (fpsTimer >= 1.0f) {
			currentFps = frameCount / fpsTimer;
			frameCount = 0;
			fpsTimer = 0.0f;
		}
		glfwPollEvents();
		processInput(window, deltaTime);
		update_ui_text(currentFps);
		animation_manager_update(&globalAnimationManager,
								 deltaTime); // Update animations
		if (globalAnimationManager.num_animations > 0) {
			renderer_update_graph(&renderer, &currentGraph);
		}
		if (currentLayout == LAYOUT_OPENORD_3D && currentGraph.openord &&
			currentGraph.openord->stage_id < 5) {
			graph_layout_step(&currentGraph, currentLayout, 1);
			renderer_update_graph(&renderer, &currentGraph);
		} else if (currentLayout == LAYOUT_LAYERED_SPHERE &&
				   currentGraph.layered_sphere) {
			graph_layout_step(&currentGraph, currentLayout, 1);
			renderer_update_graph(&renderer, &currentGraph);
		}

		renderer_update_view(&renderer, cameraPos, cameraFront, cameraUp);
		renderer_draw_frame(&renderer);
	}
	graph_free_data(&currentGraph);
	animation_manager_cleanup(&globalAnimationManager); // Cleanup animations
	renderer_cleanup(&renderer);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
