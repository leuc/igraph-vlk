#include "ui/sphere_menu.h"
#include "graph/wrappers.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"
#include "interaction/camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern FontAtlas globalAtlas;

// Helper function to create a menu node
static MenuNode *create_menu_node(const char *label, MenuNodeType type) {
	MenuNode *node = (MenuNode *)malloc(sizeof(MenuNode));
	node->label = strdup(label);
	node->type = type;
	node->icon_texture_id = -1;
	node->target_phi = 0.0f;
	node->target_theta = 0.0f;
	node->current_radius = 0.0f;
	node->target_radius = 0.0f; // Start collapsed
	node->num_children = 0;
	node->children = NULL;
	node->command = NULL;
	node->is_expanded = false;
	return node;
}

// Helper function to create a command
static IgraphCommand *create_command(const char *id_name,
									 const char *display_name,
									 IgraphWrapperFunc execute,
									 int num_params) {
	IgraphCommand *cmd = (IgraphCommand *)malloc(sizeof(IgraphCommand));
	cmd->id_name = strdup(id_name);
	cmd->display_name = strdup(display_name);
	cmd->icon_path = NULL;
	cmd->execute = execute;
	cmd->num_params = num_params;
	cmd->params =
		(CommandParameter *)malloc(sizeof(CommandParameter) * num_params);
	cmd->produces_visual_output = false;
	return cmd;
}

// Forward declarations
static void assign_menu_icons(MenuNode *node);

// Initialize the menu tree with the full requested structure
void init_menu_tree(MenuNode *root) {
    enum {
        MENU_ROOT,
        // 1. File
        MENU_FILE, MENU_FILE_NEW, MENU_FILE_OPEN, MENU_FILE_SAVE, MENU_FILE_EXIT,
        // 2. Generate
        MENU_GENERATE,
        MENU_GEN_DET, MENU_GEN_DET_RING, MENU_GEN_DET_STAR, MENU_GEN_DET_TREE, MENU_GEN_DET_LATTICE, MENU_GEN_DET_FULL, MENU_GEN_DET_CIRCLE, MENU_GEN_DET_NOTABLE,
        MENU_GEN_STO, MENU_GEN_STO_ER, MENU_GEN_STO_BA, MENU_GEN_STO_WS, MENU_GEN_STO_FF, MENU_GEN_STO_TREE, MENU_GEN_STO_DEG,
        MENU_GEN_BIP, MENU_GEN_BIP_RAND, MENU_GEN_BIP_PROJ,
        MENU_GEN_SPA, MENU_GEN_SPA_GEO, MENU_GEN_SPA_GAB,
        // 4. Layout
        MENU_LAYOUT,
        MENU_LAY_FORCE, MENU_LAY_FORCE_FR, MENU_LAY_FORCE_KK, MENU_LAY_FORCE_DRL, MENU_LAY_FORCE_GOPT,
        MENU_LAY_TREE, MENU_LAY_TREE_RT, MENU_LAY_TREE_SUG,
        MENU_LAY_GEO, MENU_LAY_GEO_CIRCLE, MENU_LAY_GEO_STAR, MENU_LAY_GEO_GRID, MENU_LAY_GEO_SPHERE, MENU_LAY_GEO_RAND,
        MENU_LAY_BIP, MENU_LAY_BIP_MDS, MENU_LAY_BIP_SUG,
        // 5. Analysis
        MENU_ANALYSIS,
        MENU_ANA_CENT, MENU_ANA_CENT_DEG, MENU_ANA_CENT_CLO, MENU_ANA_CENT_BTW, MENU_ANA_CENT_EIG, MENU_ANA_CENT_PAGER, MENU_ANA_CENT_HITS,
        MENU_ANA_GLOB, MENU_ANA_GLOB_DIAM, MENU_ANA_GLOB_RAD, MENU_ANA_GLOB_APL, MENU_ANA_GLOB_ASSORT, MENU_ANA_GLOB_MOD, MENU_ANA_GLOB_DENS, MENU_ANA_GLOB_TRANS,
        MENU_ANA_DIST, MENU_ANA_DIST_SP, MENU_ANA_DIST_SP_DIJ, MENU_ANA_DIST_SP_BF, MENU_ANA_DIST_SP_ASTAR, MENU_ANA_DIST_SP_JOH, MENU_ANA_DIST_ECC,
        MENU_ANA_FLOW, MENU_ANA_FLOW_MAX, MENU_ANA_FLOW_MIN, MENU_ANA_FLOW_GOM, MENU_ANA_FLOW_ADH,
        MENU_ANA_EMB, MENU_ANA_EMB_ADJ, MENU_ANA_EMB_LAP,
        // 6. Topology & Motifs
        MENU_TOPOLOGY,
        MENU_TOP_COMP, MENU_TOP_COMP_CONN, MENU_TOP_COMP_BICON, MENU_TOP_COMP_ART, MENU_TOP_COMP_SEP,
        MENU_TOP_CLIQ, MENU_TOP_CLIQ_MAXL, MENU_TOP_CLIQ_MAXM, MENU_TOP_CLIQ_IND,
        MENU_TOP_CYC, MENU_TOP_CYC_EUL, MENU_TOP_CYC_FUND,
        MENU_TOP_MOT, MENU_TOP_MOT_TRI, MENU_TOP_MOT_DYA, MENU_TOP_MOT_NET,
        MENU_TOP_ISO, MENU_TOP_ISO_VF2, MENU_TOP_ISO_BLISS,
        MENU_TOP_COL, MENU_TOP_COL_GREEDY,
        // 7. Communities
        MENU_COMMUNITIES,
        MENU_COM_DET, MENU_COM_DET_LOUV, MENU_COM_DET_WALK, MENU_COM_DET_BTW, MENU_COM_DET_FAST, MENU_COM_DET_INFO, MENU_COM_DET_LAB, MENU_COM_DET_SPIN,
        MENU_COM_COMP, MENU_COM_COMP_SJ, MENU_COM_COMP_ARI,
        MENU_COM_GLET, MENU_COM_GLET_BASIS, MENU_COM_GLET_PROJ,
        MENU_COM_HRG, MENU_COM_HRG_FIT, MENU_COM_HRG_CONS,
        // 8. Processes & Traversal
        MENU_PROCESSES,
        MENU_PRO_SEARCH, MENU_PRO_SEARCH_BFS, MENU_PRO_SEARCH_DFS, MENU_PRO_SEARCH_TOPO,
        MENU_PRO_PROC, MENU_PRO_PROC_RW, MENU_PRO_PROC_RT,
        MENU_COUNT
    };

    const MenuDefinition definitions[] = {
        { "Main", NODE_BRANCH, (int[]){ MENU_FILE, MENU_GENERATE, MENU_LAYOUT, MENU_ANALYSIS, MENU_TOPOLOGY, MENU_COMMUNITIES, MENU_PROCESSES, -1 }, NULL, 0 },
        
        // 1. File
        { "File", NODE_BRANCH, (int[]){ MENU_FILE_NEW, MENU_FILE_OPEN, MENU_FILE_SAVE, MENU_FILE_EXIT, -1 }, NULL, 1 },
        { "New Graph", NODE_LEAF, NULL, "file_new", 2 },
        { "Open Graph", NODE_LEAF, NULL, "file_open", 2 },
        { "Save Graph", NODE_LEAF, NULL, "file_save", 2 },
        { "Exit", NODE_LEAF, NULL, "app_exit", 2 },

        // 2. Generate
        { "Generate", NODE_BRANCH, (int[]){ MENU_GEN_DET, MENU_GEN_STO, MENU_GEN_BIP, MENU_GEN_SPA, -1 }, NULL, 1 },
        { "Deterministic Graphs", NODE_BRANCH, (int[]){ MENU_GEN_DET_RING, MENU_GEN_DET_STAR, MENU_GEN_DET_TREE, MENU_GEN_DET_LATTICE, MENU_GEN_DET_FULL, MENU_GEN_DET_CIRCLE, MENU_GEN_DET_NOTABLE, -1 }, NULL, 2 },
        { "Ring", NODE_LEAF, NULL, "gen_det_ring", 3 },
        { "Star", NODE_LEAF, NULL, "gen_det_star", 3 },
        { "Tree", NODE_LEAF, NULL, "gen_det_tree", 3 },
        { "Lattice", NODE_LEAF, NULL, "gen_det_lattice", 3 },
        { "Full Graph (Clique)", NODE_LEAF, NULL, "gen_det_full", 3 },
        { "Circle", NODE_LEAF, NULL, "gen_det_circle", 3 },
        { "Notable Graphs", NODE_LEAF, NULL, "gen_det_notable", 3 },
        { "Stochastic Graphs", NODE_BRANCH, (int[]){ MENU_GEN_STO_ER, MENU_GEN_STO_BA, MENU_GEN_STO_WS, MENU_GEN_STO_FF, MENU_GEN_STO_TREE, MENU_GEN_STO_DEG, -1 }, NULL, 2 },
        { "Erdős-Rényi", NODE_LEAF, NULL, "gen_sto_er", 3 },
        { "Barabási-Albert", NODE_LEAF, NULL, "gen_sto_ba", 3 },
        { "Watts-Strogatz", NODE_LEAF, NULL, "gen_sto_ws", 3 },
        { "Forest Fire", NODE_LEAF, NULL, "gen_sto_ff", 3 },
        { "Random Tree", NODE_LEAF, NULL, "gen_sto_tree", 3 },
        { "Degree Sequence", NODE_LEAF, NULL, "gen_sto_deg", 3 },
        { "Bipartite Graphs", NODE_BRANCH, (int[]){ MENU_GEN_BIP_RAND, MENU_GEN_BIP_PROJ, -1 }, NULL, 2 },
        { "Generate Random Bipartite", NODE_LEAF, NULL, "gen_bip_rand", 3 },
        { "Create Bipartite Projections", NODE_LEAF, NULL, "gen_bip_proj", 3 },
        { "Spatial Graphs", NODE_BRANCH, (int[]){ MENU_GEN_SPA_GEO, MENU_GEN_SPA_GAB, -1 }, NULL, 2 },
        { "Geometric Random Graphs", NODE_LEAF, NULL, "gen_spa_geo", 3 },
        { "Gabriel Graphs", NODE_LEAF, NULL, "gen_spa_gab", 3 },

        // 4. Layout
        { "Layout", NODE_BRANCH, (int[]){ MENU_LAY_FORCE, MENU_LAY_TREE, MENU_LAY_GEO, MENU_LAY_BIP, -1 }, NULL, 1 },
        { "Force-Directed Layouts", NODE_BRANCH, (int[]){ MENU_LAY_FORCE_FR, MENU_LAY_FORCE_KK, MENU_LAY_FORCE_DRL, MENU_LAY_FORCE_GOPT, -1 }, NULL, 2 },
        { "Fruchterman-Reingold", NODE_LEAF, NULL, "lay_force_fr", 3 },
        { "Kamada-Kawai", NODE_LEAF, NULL, "lay_force_kk", 3 },
        { "Distributed Recursive", NODE_LEAF, NULL, "lay_force_drl", 3 },
        { "GraphOpt", NODE_LEAF, NULL, "lay_force_gopt", 3 },
        { "Tree & Hierarchical", NODE_BRANCH, (int[]){ MENU_LAY_TREE_RT, MENU_LAY_TREE_SUG, -1 }, NULL, 2 },
        { "Reingold-Tilford", NODE_LEAF, NULL, "lay_tree_rt", 3 },
        { "Sugiyama", NODE_LEAF, NULL, "lay_tree_sug", 3 },
        { "Geometric Layouts", NODE_BRANCH, (int[]){ MENU_LAY_GEO_CIRCLE, MENU_LAY_GEO_STAR, MENU_LAY_GEO_GRID, MENU_LAY_GEO_SPHERE, MENU_LAY_GEO_RAND, -1 }, NULL, 2 },
        { "Circle", NODE_LEAF, NULL, "lay_geo_circle", 3 },
        { "Star", NODE_LEAF, NULL, "lay_geo_star", 3 },
        { "Grid", NODE_LEAF, NULL, "lay_geo_grid", 3 },
        { "Sphere", NODE_LEAF, NULL, "lay_geo_sphere", 3 },
        { "Random", NODE_LEAF, NULL, "lay_geo_rand", 3 },
        { "Bipartite Layouts", NODE_BRANCH, (int[]){ MENU_LAY_BIP_MDS, MENU_LAY_BIP_SUG, -1 }, NULL, 2 },
        { "MDS", NODE_LEAF, NULL, "lay_bip_mds", 3 },
        { "Sugiyama (Bipartite)", NODE_LEAF, NULL, "lay_bip_sug", 3 },

        // 5. Analysis
        { "Analysis", NODE_BRANCH, (int[]){ MENU_ANA_CENT, MENU_ANA_GLOB, MENU_ANA_DIST, MENU_ANA_FLOW, MENU_ANA_EMB, -1 }, NULL, 1 },
        { "Centrality & Roles", NODE_BRANCH, (int[]){ MENU_ANA_CENT_DEG, MENU_ANA_CENT_CLO, MENU_ANA_CENT_BTW, MENU_ANA_CENT_EIG, MENU_ANA_CENT_PAGER, MENU_ANA_CENT_HITS, -1 }, NULL, 2 },
        { "Degree", NODE_LEAF, NULL, "ana_cent_deg", 3 },
        { "Closeness", NODE_LEAF, NULL, "ana_cent_clo", 3 },
        { "Betweenness", NODE_LEAF, NULL, "ana_cent_btw", 3 },
        { "Eigenvector Centrality", NODE_LEAF, NULL, "ana_cent_eig", 3 },
        { "PageRank", NODE_LEAF, NULL, "ana_cent_pager", 3 },
        { "Hubs & Authorities", NODE_LEAF, NULL, "ana_cent_hits", 3 },
        { "Global Network Properties", NODE_BRANCH, (int[]){ MENU_ANA_GLOB_DIAM, MENU_ANA_GLOB_RAD, MENU_ANA_GLOB_APL, MENU_ANA_GLOB_ASSORT, MENU_ANA_GLOB_MOD, MENU_ANA_GLOB_DENS, MENU_ANA_GLOB_TRANS, -1 }, NULL, 2 },
        { "Diameter", NODE_LEAF, NULL, "ana_glob_diam", 3 },
        { "Radius", NODE_LEAF, NULL, "ana_glob_rad", 3 },
        { "Average Path Length", NODE_LEAF, NULL, "ana_glob_apl", 3 },
        { "Assortativity", NODE_LEAF, NULL, "ana_glob_assort", 3 },
        { "Modularity", NODE_LEAF, NULL, "ana_glob_mod", 3 },
        { "Density", NODE_LEAF, NULL, "ana_glob_dens", 3 },
        { "Transitivity", NODE_LEAF, NULL, "ana_glob_trans", 3 },
        { "Distances & Paths", NODE_BRANCH, (int[]){ MENU_ANA_DIST_SP, MENU_ANA_DIST_ECC, -1 }, NULL, 2 },
        { "Shortest Paths", NODE_BRANCH, (int[]){ MENU_ANA_DIST_SP_DIJ, MENU_ANA_DIST_SP_BF, MENU_ANA_DIST_SP_ASTAR, MENU_ANA_DIST_SP_JOH, -1 }, NULL, 3 },
        { "Dijkstra", NODE_LEAF, NULL, "ana_dist_sp_dij", 4 },
        { "Bellman-Ford", NODE_LEAF, NULL, "ana_dist_sp_bf", 4 },
        { "A*", NODE_LEAF, NULL, "ana_dist_sp_astar", 4 },
        { "Johnson", NODE_LEAF, NULL, "ana_dist_sp_joh", 4 },
        { "Eccentricity", NODE_LEAF, NULL, "ana_dist_ecc", 3 },
        { "Flows & Cuts", NODE_BRANCH, (int[]){ MENU_ANA_FLOW_MAX, MENU_ANA_FLOW_MIN, MENU_ANA_FLOW_GOM, MENU_ANA_FLOW_ADH, -1 }, NULL, 2 },
        { "Maximum Flow", NODE_LEAF, NULL, "ana_flow_max", 3 },
        { "Minimum Cut", NODE_LEAF, NULL, "ana_flow_min", 3 },
        { "Gomory-Hu Tree", NODE_LEAF, NULL, "ana_flow_gom", 3 },
        { "Adhesion / Cohesion", NODE_LEAF, NULL, "ana_flow_adh", 3 },
        { "Graph Embedding", NODE_BRANCH, (int[]){ MENU_ANA_EMB_ADJ, MENU_ANA_EMB_LAP, -1 }, NULL, 2 },
        { "Adjacency Spectral", NODE_LEAF, NULL, "ana_emb_adj", 3 },
        { "Laplacian Spectral", NODE_LEAF, NULL, "ana_emb_lap", 3 },

        // 6. Topology & Motifs
        { "Topology & Motifs", NODE_BRANCH, (int[]){ MENU_TOP_COMP, MENU_TOP_CLIQ, MENU_TOP_CYC, MENU_TOP_MOT, MENU_TOP_ISO, MENU_TOP_COL, -1 }, NULL, 1 },
        { "Components & Connectivity", NODE_BRANCH, (int[]){ MENU_TOP_COMP_CONN, MENU_TOP_COMP_BICON, MENU_TOP_COMP_ART, MENU_TOP_COMP_SEP, -1 }, NULL, 2 },
        { "Connected Components", NODE_LEAF, NULL, "top_comp_conn", 3 },
        { "Biconnected Components", NODE_LEAF, NULL, "top_comp_bicon", 3 },
        { "Articulation Points", NODE_LEAF, NULL, "top_comp_art", 3 },
        { "Vertex Separators", NODE_LEAF, NULL, "top_comp_sep", 3 },
        { "Cliques & Independent Sets", NODE_BRANCH, (int[]){ MENU_TOP_CLIQ_MAXL, MENU_TOP_CLIQ_MAXM, MENU_TOP_CLIQ_IND, -1 }, NULL, 2 },
        { "Maximal Cliques", NODE_LEAF, NULL, "top_cliq_maxl", 3 },
        { "Maximum Cliques", NODE_LEAF, NULL, "top_cliq_maxm", 3 },
        { "Independent Sets", NODE_LEAF, NULL, "top_cliq_ind", 3 },
        { "Cycles & Paths", NODE_BRANCH, (int[]){ MENU_TOP_CYC_EUL, MENU_TOP_CYC_FUND, -1 }, NULL, 2 },
        { "Eulerian Paths", NODE_LEAF, NULL, "top_cyc_eul", 3 },
        { "Fundamental Cycles", NODE_LEAF, NULL, "top_cyc_fund", 3 },
        { "Motifs & Censuses", NODE_BRANCH, (int[]){ MENU_TOP_MOT_TRI, MENU_TOP_MOT_DYA, MENU_TOP_MOT_NET, -1 }, NULL, 2 },
        { "Triad Census", NODE_LEAF, NULL, "top_mot_tri", 3 },
        { "Dyad Census", NODE_LEAF, NULL, "top_mot_dya", 3 },
        { "Network Motifs", NODE_LEAF, NULL, "top_mot_net", 3 },
        { "Graph Isomorphism", NODE_BRANCH, (int[]){ MENU_TOP_ISO_VF2, MENU_TOP_ISO_BLISS, -1 }, NULL, 2 },
        { "VF2", NODE_LEAF, NULL, "top_iso_vf2", 3 },
        { "Bliss", NODE_LEAF, NULL, "top_iso_bliss", 3 },
        { "Graph Coloring", NODE_BRANCH, (int[]){ MENU_TOP_COL_GREEDY, -1 }, NULL, 2 },
        { "Greedy Coloring", NODE_LEAF, NULL, "top_col_greedy", 3 },

        // 7. Communities
        { "Communities", NODE_BRANCH, (int[]){ MENU_COM_DET, MENU_COM_COMP, MENU_COM_GLET, MENU_COM_HRG, -1 }, NULL, 1 },
        { "Detect Community Structure", NODE_BRANCH, (int[]){ MENU_COM_DET_LOUV, MENU_COM_DET_WALK, MENU_COM_DET_BTW, MENU_COM_DET_FAST, MENU_COM_DET_INFO, MENU_COM_DET_LAB, MENU_COM_DET_SPIN, -1 }, NULL, 2 },
        { "Louvain Method", NODE_LEAF, NULL, "com_det_louv", 3 },
        { "Walktrap", NODE_LEAF, NULL, "com_det_walk", 3 },
        { "Edge Betweenness", NODE_LEAF, NULL, "com_det_btw", 3 },
        { "Fast Greedy", NODE_LEAF, NULL, "com_det_fast", 3 },
        { "Infomap", NODE_LEAF, NULL, "com_det_info", 3 },
        { "Label Propagation", NODE_LEAF, NULL, "com_det_lab", 3 },
        { "Spinglass", NODE_LEAF, NULL, "com_det_spin", 3 },
        { "Compare Communities", NODE_BRANCH, (int[]){ MENU_COM_COMP_SJ, MENU_COM_COMP_ARI, -1 }, NULL, 2 },
        { "Split-join Distance", NODE_LEAF, NULL, "com_comp_sj", 3 },
        { "Adjusted Rand Index", NODE_LEAF, NULL, "com_comp_ari", 3 },
        { "Graphlets", NODE_BRANCH, (int[]){ MENU_COM_GLET_BASIS, MENU_COM_GLET_PROJ, -1 }, NULL, 2 },
        { "Basis", NODE_LEAF, NULL, "com_glet_basis", 3 },
        { "Projections", NODE_LEAF, NULL, "com_glet_proj", 3 },
        { "Hierarchical Random Graphs", NODE_BRANCH, (int[]){ MENU_COM_HRG_FIT, MENU_COM_HRG_CONS, -1 }, NULL, 2 },
        { "Fit", NODE_LEAF, NULL, "com_hrg_fit", 3 },
        { "Consensus Trees", NODE_LEAF, NULL, "com_hrg_cons", 3 },

        // 8. Processes & Traversal
        { "Processes & Traversal", NODE_BRANCH, (int[]){ MENU_PRO_SEARCH, MENU_PRO_PROC, -1 }, NULL, 1 },
        { "Search & Traversal", NODE_BRANCH, (int[]){ MENU_PRO_SEARCH_BFS, MENU_PRO_SEARCH_DFS, MENU_PRO_SEARCH_TOPO, -1 }, NULL, 2 },
        { "Breadth-First Search", NODE_LEAF, NULL, "pro_search_bfs", 3 },
        { "Depth-First Search", NODE_LEAF, NULL, "pro_search_dfs", 3 },
        { "Topological Sorting", NODE_LEAF, NULL, "pro_search_topo", 3 },
        { "Processes on Graphs", NODE_BRANCH, (int[]){ MENU_PRO_PROC_RW, MENU_PRO_PROC_RT, -1 }, NULL, 2 },
        { "Simulate Random Walks", NODE_LEAF, NULL, "pro_proc_rw", 3 },
        { "Rank / Transition", NODE_LEAF, NULL, "pro_proc_rt", 3 },
    };

    init_menu_from_definitions(root, definitions, MENU_COUNT);
}

static void assign_menu_icons(MenuNode *node) {
	if (node == NULL)
		return;
	static int next_icon_id = 0;
	node->icon_texture_id = next_icon_id++;
	for (int i = 0; i < node->num_children; i++) {
		assign_menu_icons(node->children[i]);
	}
}

// Destroy the menu tree recursively
void destroy_menu_tree(MenuNode *node) {
	if (node == NULL)
		return;
	if (node->type == NODE_BRANCH) {
		for (int i = 0; i < node->num_children; i++) {
			destroy_menu_tree(node->children[i]);
		}
		if (node->children)
			free(node->children);
	}

	if (node->label)
		free((void *)node->label);
}

// Update menu animation with smooth easing
// Uses recursive vertical tree layout
static void update_menu_layout_recursive(MenuNode *node, float delta_time, int depth, float *current_y) {
	if (node == NULL)
		return;

	float speed = 8.0f;
	float diff = node->target_radius - node->current_radius;
	if (fabsf(diff) < 0.001f) {
		node->current_radius = node->target_radius;
	} else {
		node->current_radius += diff * speed * delta_time;
	}

	// Position THIS node
	node->target_phi = (float)(depth - 1) * 0.12f;
	node->target_theta = *current_y;
	
	// Only consume vertical space if this node is expanding/open
	// This ensures smooth stacking animation
	*current_y -= 0.12f * node->current_radius;

	// Recursively update children
	if (node->num_children > 0) {
		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];

			// Children only expand if parent is mostly open AND expanded
			child->target_radius =
				(node->current_radius > 0.5f && node->is_expanded) ? 1.0f : 0.0f;

			// Always recurse if parent is visible to ensure children close/open correctly
			if (node->current_radius > 0.001f) {
				update_menu_layout_recursive(child, delta_time, depth + 1, current_y);
			} else {
				// Parent is fully closed, snap child state
				child->current_radius = 0.0f;
				child->target_radius = 0.0f;
				child->target_phi = 0.0f;
				child->target_theta = 0.0f;
			}
		}
	}
}

void update_menu_animation(MenuNode *node, float delta_time) {
    if (node == NULL) return;
    
    // Start tracking layout from the top
    float start_y = 0.0f; 
    
    // The root node itself doesn't need indentation, its children start at depth 1
    update_menu_layout_recursive(node, delta_time, 1, &start_y);
}

// Generate Vulkan menu buffers (instanced rendering)
void generate_vulkan_menu_buffers(MenuNode *node,
								  Renderer *r,
								  vec3 cam_pos,
								  vec3 cam_front,
								  vec3 cam_up) {
	if (node == NULL)
		return;

	// Fully vanish after closing animation
	// Reset counts to 0 once we're collapsed so nothing is drawn
	if (node->target_radius == 0.0f && node->current_radius < 0.005f) {
		r->menuNodeCount = 0;
		r->menuTextCharCount = 0;
		return;
	}

	int capacity = 128;
	MenuInstance *instances =
		(MenuInstance *)malloc(sizeof(MenuInstance) * capacity);
	int instance_count = 0;

	// Label rendering
	int label_capacity = 1024;
	LabelInstance *label_instances =
		(LabelInstance *)malloc(sizeof(LabelInstance) * label_capacity);
	int label_count = 0;

	// Stack-based traversal
	MenuNode **stack = (MenuNode **)malloc(sizeof(MenuNode *) * 256);
	int stack_top = 0;
	stack[stack_top++] = node;

	// Basis vectors for billboarding (using cam_front as camera front)
	vec3 right, up;
    glm_vec3_cross(cam_front, cam_up, right);
	glm_vec3_normalize(right);
	glm_vec3_cross(right, cam_front, up);
	glm_vec3_normalize(up);

	while (stack_top > 0) {
		MenuNode *current = stack[--stack_top];
		if (current == NULL)
			continue;

		// Render every visible node except the root container if it's at (0,0)
		if (current->current_radius > 0.01f) {
			if (instance_count >= capacity) {
				capacity *= 2;
				instances = (MenuInstance *)realloc(
					instances, sizeof(MenuInstance) * capacity);
			}

			// Simple billboard position: cam_pos + distance*front + x*right + y*up
            // Offset adjusted to be a bit more centered (-0.8m) and further away (2.5m)
			float x_off = current->target_phi - 0.8f;
			float y_off = current->target_theta;

			vec3 world_pos;
			glm_vec3_copy(cam_pos, world_pos);

			vec3 f_part, r_part, u_part;
			glm_vec3_scale(cam_front, 2.5f, f_part); // 2.5 meters away
			glm_vec3_scale(right, x_off, r_part);
			glm_vec3_scale(up, y_off, u_part);

			glm_vec3_add(world_pos, f_part, world_pos);
			glm_vec3_add(world_pos, r_part, world_pos);
			glm_vec3_add(world_pos, u_part, world_pos);

			// 2D tree layout - no spherical scaling needed
			// world_pos already contains the correct flat position

			glm_vec3_copy(world_pos, instances[instance_count].worldPos);
			instances[instance_count].texCoord[0] = 0.0f;
			instances[instance_count].texCoord[1] = 0.0f;
			instances[instance_count].texId = (float)current->icon_texture_id;

			// Dynamic box sizing based on text - Tightened to match font
			float world_text_scale = 0.003f; // Font bake size (32px) to world units
			float padding_x = 0.08f;   // Tight padding
			float fixed_height = 0.09f; // Consistent height

			// Calculate total width for dynamic sizing (even if no label)
			float total_w = 0.0f;
			int label_len = 0;
			if (current->label) {
				label_len = strlen(current->label);
				for (int i = 0; i < label_len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c]
											 : &globalAtlas.chars[32];
					total_w += ci->xadvance;
				}
			}

			// Use dynamic bounds: width based on text + padding, height fixed
			float box_width = (total_w * world_text_scale) + padding_x;
			instances[instance_count].scale[0] = box_width * current->current_radius; 
			instances[instance_count].scale[1] = fixed_height * current->current_radius;
			instances[instance_count].scale[2] = 1.0f; 
            instances[instance_count].hovered = current->hovered ? 1.0f : 0.0f;

			// Center the background quad around the text anchor
			if (current->label) {
				vec3 offset;
				glm_vec3_scale(right, box_width * 0.5f, offset);
				glm_vec3_add(instances[instance_count].worldPos, offset,
							 instances[instance_count].worldPos);
			}

            // Calculate rotation quaternion to align quad (default XY plane) with camera
            // Camera orientation: front is Z (backwards), right is X, up is Y
            // We want the quad's +Z to face the camera's -front
            mat3 rot_mat;
            vec3 neg_front;
            glm_vec3_negate_to(cam_front, neg_front);
            glm_mat3_identity(rot_mat);
            glm_mat3_copy((mat3){
                {right[0], right[1], right[2]},
                {up[0],    up[1],    up[2]},
                {neg_front[0], neg_front[1], neg_front[2]}
            }, rot_mat);
            glm_mat3_quat(rot_mat, instances[instance_count].rotation);

			// Generate label instances
			if (current->label && current->current_radius > 0.01f) { // Render text as long as node is visible
				int len = strlen(current->label);
				if (label_count + len >= label_capacity) {
					label_capacity *= 2;
					label_instances = (LabelInstance *)realloc(
						label_instances,
						sizeof(LabelInstance) * label_capacity);
				}

				float x_cursor = 0.0f;
				// x_cursor = -total_w * 0.5f; // old centering
				x_cursor = (padding_x * 0.4f) / world_text_scale; // Left-aligned with a small margin

				for (int i = 0; i < len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c]
											 : &globalAtlas.chars[32];

					// Offset text forward from the quad slightly to prevent z-fighting
					vec3 label_pos;
					glm_vec3_copy(world_pos, label_pos);
					vec3 forward_off, down_off;
					glm_vec3_scale(cam_front, -0.002f, forward_off); // Move slightly towards camera
					glm_vec3_scale(up, -0.015f, down_off); // Centering adjustment
					glm_vec3_add(label_pos, forward_off, label_pos);
					glm_vec3_add(label_pos, down_off, label_pos);

					glm_vec3_copy(label_pos, label_instances[label_count].nodePos);
					label_instances[label_count].charRect[0] = x_cursor + ci->x0;
					label_instances[label_count].charRect[1] = ci->y0;
					label_instances[label_count].charRect[2] = x_cursor + ci->x1;
					label_instances[label_count].charRect[3] = ci->y1;
					label_instances[label_count].charUV[0] = ci->u0;
					label_instances[label_count].charUV[1] = ci->v0;
					label_instances[label_count].charUV[2] = ci->u1;
					label_instances[label_count].charUV[3] = ci->v1;
					// Scale orientation vectors by current_radius so text shrinks/vanishes with the quad
					float dynamic_scale = world_text_scale * current->current_radius;
					glm_vec3_scale(right, dynamic_scale, label_instances[label_count].right);
					glm_vec3_scale(up, dynamic_scale, label_instances[label_count].up);

					x_cursor += ci->xadvance;
					label_count++;
				}
			}

			instance_count++;
		}

		for (int i = 0; i < current->num_children; i++) {
			stack[stack_top++] = current->children[i];
		}
	}

	free(stack);

	if (instance_count > 0) {
		if (r->menuQuadVertexBuffer == VK_NULL_HANDLE) {
			QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
							   {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
							   {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
							   {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
			uint32_t qi[] = {0, 1, 2, 2, 3, 0};

			createBuffer(r->device, r->physicalDevice, sizeof(qv),
						 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &r->menuQuadVertexBuffer,
						 &r->menuQuadVertexBufferMemory);
			updateBuffer(r->device, r->menuQuadVertexBufferMemory, sizeof(qv),
						 qv);

			createBuffer(r->device, r->physicalDevice, sizeof(qi),
						 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &r->menuQuadIndexBuffer,
						 &r->menuQuadIndexBufferMemory);
			updateBuffer(r->device, r->menuQuadIndexBufferMemory, sizeof(qi),
						 qi);
			r->menuQuadIndexCount = 6;
		}

		VkDeviceSize bufferSize = sizeof(MenuInstance) * instance_count;
		if (r->menuInstanceBuffer != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(r->device); // Ensure GPU is done with it
			vkDestroyBuffer(r->device, r->menuInstanceBuffer, NULL);
			vkFreeMemory(r->device, r->menuInstanceBufferMemory, NULL);
		}
		createBuffer(r->device, r->physicalDevice, bufferSize,
					 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->menuInstanceBuffer, &r->menuInstanceBufferMemory);
		updateBuffer(r->device, r->menuInstanceBufferMemory, bufferSize,
					 instances);
		r->menuNodeCount = instance_count;

		// Update label buffer
		if (label_count > 0) {
			VkDeviceSize labelBufferSize = sizeof(LabelInstance) * label_count;
			if (r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(r->device, r->menuTextInstanceBuffer, NULL);
				vkFreeMemory(r->device, r->menuTextInstanceBufferMemory, NULL);
			}
			createBuffer(r->device, r->physicalDevice, labelBufferSize,
						 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &r->menuTextInstanceBuffer,
						 &r->menuTextInstanceBufferMemory);
			updateBuffer(r->device, r->menuTextInstanceBufferMemory,
						 labelBufferSize, label_instances);
			r->menuTextCharCount = label_count;
		}
	}

	free(instances);
	free(label_instances);
}

MenuNode* find_menu_node(MenuNode* root, const char* label) {
    if (root == NULL || label == NULL) return NULL;
    if (strcmp(root->label, label) == 0) return root;
    for (int i = 0; i < root->num_children; i++) {
        MenuNode* res = find_menu_node(root->children[i], label);
        if (res) return res;
    }
    return NULL;
}

// Data-driven initialization helper
void init_menu_from_definitions(MenuNode* root, const MenuDefinition* definitions, int num_definitions) {
    if (num_definitions <= 0) return;

    // First pass: Create all nodes
    MenuNode** nodes = (MenuNode**)malloc(sizeof(MenuNode*) * num_definitions);
    for (int i = 0; i < num_definitions; i++) {
        nodes[i] = create_menu_node(definitions[i].label, definitions[i].type);
        if (definitions[i].type == NODE_LEAF && definitions[i].command_id) {
            // Assign placeholder commands
            nodes[i]->command = create_command(definitions[i].command_id, definitions[i].label, NULL, 0);
        }
    }

    // Second pass: Link children
    for (int i = 0; i < num_definitions; i++) {
        if (definitions[i].type == NODE_BRANCH && definitions[i].child_ids) {
            int count = 0;
            while (definitions[i].child_ids[count] != -1) count++;
            
            nodes[i]->num_children = count;
            nodes[i]->children = (MenuNode**)malloc(sizeof(MenuNode*) * count);
            for (int j = 0; j < count; j++) {
                int child_idx = definitions[i].child_ids[j];
                if (child_idx >= 0 && child_idx < num_definitions) {
                    nodes[i]->children[j] = nodes[child_idx];
                }
            }
        }
    }

    // Root is the first node in the array
    *root = *nodes[0];
    root->is_expanded = true; // Ensure top level is visible
    
    // Free the nodes array (pointers are now in the tree)
    free(nodes);

    assign_menu_icons(root);
}
