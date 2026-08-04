/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_TYPES_H
#define VULKAN_TYPES_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <igraph.h>
#include <igraph_barnes_hut.h>
#include <vulkan/vulkan.h>

#include <stdbool.h>

#include "graph/graph_types.h"

#include "vulkan/text.h"

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_VIEWS 3
#define GRAPH_UPDATE_RING_SIZE 3

typedef enum { ROUTING_MODE_STRAIGHT = 0, ROUTING_MODE_SPHERICAL_PCB = 1 } EdgeRoutingMode;

// SPLC compute buffer types
typedef struct
{
	uint32_t edge_offset;
	uint32_t out_degree;
} SPLCNode;

typedef struct
{
	uint32_t target_node;
	float weight;
} SPLCEdge;

// Generalised criticality compute types (arXiv:2512.12355 section 2.5).
// Same CSR shape as SPLCNode/SPLCEdge, but used for both the forward
// (out-edge) and reverse (in-edge) adjacency, so the field names are
// direction-neutral.
typedef struct
{
	uint32_t edge_offset;
	uint32_t degree;
} CritNode;

typedef struct
{
	uint32_t node;
	uint32_t pad;
} CritEdge;

// Must match the stage/weight_mode constants in shaders/main_path.comp
typedef enum { CRIT_STAGE_LNW = 0, CRIT_STAGE_LNX = 1, CRIT_STAGE_HEIGHT = 2, CRIT_STAGE_DEPTH = 3 } CritStage;

typedef enum { CRIT_WEIGHT_SPLC = 0, CRIT_WEIGHT_UNIT = 1, CRIT_WEIGHT_SPC = 2, CRIT_WEIGHT_SPE = 3 } CritWeightMode;

typedef struct
{
	uint32_t level_offset;
	uint32_t num_nodes_in_level;
	uint32_t stage;
	uint32_t weight_mode;
} CritPushConstants;

// BCGL (Binary Classification-Based Graph Layout) compute types
typedef struct
{
	vec3 pos;
	float pad0;
	vec3 velocity;
	float pad1;
} BCGLNodeData;

// BCGL specific CSR Topology
typedef struct
{
	uint32_t edge_offset;
	uint32_t out_degree;
} BCGLTopoNode;

typedef struct
{
	uint32_t target_node;
	uint32_t pad; // 8-byte alignment
} BCGLTopoEdge;

typedef struct
{
	uint32_t vertexCount;
	uint32_t edgeCount;
	float lambda_bc;
	float lambda_compact;
	float lambda_length;
	float learning_rate;
	float momentum;
	float b;
} BCGLPushConstants;

typedef struct
{
	VkBuffer node_buf;
	VkDeviceMemory node_mem;
	VkBuffer topo_nodes_buf;
	VkDeviceMemory topo_nodes_mem;
	VkBuffer topo_edges_buf;
	VkDeviceMemory topo_edges_mem;
	VkDescriptorPool pool;
	VkDescriptorSet desc_set;
	VkPipeline pipeline;
	VkPipelineLayout layout;
	VkDescriptorSetLayout desc_layout;
	uint32_t capacity_nodes;
	uint32_t capacity_edges;
	VkFence fence;
	VkCommandPool cmd_pool;
	VkCommandBuffer cmd_buf;
	bool active;
	uint32_t iterations_dispatched;
	uint32_t total_iterations;
} BCGLComputeContext;

typedef struct
{
	int graphicsFamily;
	int presentFamily;
	int computeFamily;
} VkQueueFamilyInfo;

// --- GEOMETRY TYPES ---

typedef struct
{
	vec3 pos;
} NodePosition;

typedef struct
{
	vec3 color;
	float size;
	int degree;
	float selected;
	float visible;
} NodeAttribute;

typedef struct
{
	vec3 pos;
} EdgePosition;

typedef struct
{
	vec3 color;
	float selected;
	float normalized_pos;
	float visible;
	float alpha;
} EdgeAttribute;

typedef struct
{
	vec3 pos;
	vec2 tex;
} QuadVertex;

typedef struct
{
	vec3 pos;
	vec2 tex;
} LabelVertex;

typedef struct
{
	vec3 worldPos;
	vec4 bgColor;
	vec3 scale;
	vec3 right;
	vec3 up;
	vec4 textUV;
	vec4 textRegion;
} NodeLabelInstance;

typedef struct
{
	vec3 pos;
	vec2 tex;
} UIVertex;

typedef struct
{
	vec2 screenPos;
	vec4 charRect;
	vec4 charUV;
	vec4 color;
} UIInstance;

typedef struct
{
	vec3 worldPos;
	vec2 texCoord;
	float texId;
	vec3 scale;
	vec4 rotation;
	float hovered;
} MenuInstance;

typedef struct
{
	vec3 worldPos;	 // Quad center position
	vec4 bgColor;	 // Background color (RGBA). a==0 means text-only (no background)
	vec3 scale;		 // (width, height, 1.0)
	vec4 rotation;	 // Quaternion
	vec4 textUV;	 // (u0, v0, u1, v1) in text atlas. (0,0,0,0) = no text
	vec4 textRegion; // (left, top, right, bottom) text area in quad-local [0..1]
} TextQuadInstance;

typedef struct
{
	float dist;
	uint32_t idx;
} DistIdxPair;

// --- CONTEXT TYPES ---

typedef struct
{
	mat4 model;
	mat4 view;
	mat4 proj;
} UniformBufferObject;

typedef struct
{
	VkBuffer *buffers;
	VkDeviceMemory *memory;
	void *mapped[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	UniformBufferObject data;
} UniformBuffers;

typedef struct
{
	float time;
	float delta_time;
	uint32_t frame_count;
	float transition_t;
	float seq_time;
	float seq_stride;
	float seq_duration;
	float _reserved;
} GlobalAnimState;

_Static_assert(sizeof(GlobalAnimState) == 32, "GlobalAnimState must match the std140 block in graphics shaders");

typedef struct
{
	VkBuffer buffers[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	VkDeviceMemory memory[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	void *mapped[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	GlobalAnimState data;
	float seq_start_time;
	struct
	{
		VkBuffer node_step;
		VkDeviceMemory node_step_memory;
		VkBuffer node_value;
		VkDeviceMemory node_value_memory;
		VkBuffer edge_source;
		VkDeviceMemory edge_source_memory;
		VkBuffer edge_value;
		VkDeviceMemory edge_value_memory;
		VkBuffer edge_event_offsets;
		VkDeviceMemory edge_event_offsets_memory;
		VkBuffer edge_events;
		VkDeviceMemory edge_events_memory;
		uint32_t node_capacity;
		uint32_t edge_capacity;
		uint32_t edge_event_offset_capacity;
		uint32_t edge_event_capacity;
	} channels;
} AnimStateBuffers;

typedef struct
{
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkSampler sampler;
} Texture;

typedef struct
{
	VkBuffer nodeBuf;
	VkDeviceMemory nodeMem;
	VkBuffer edgeBuf;
	VkDeviceMemory edgeMem;
	VkBuffer hubBuf;
	VkDeviceMemory hubMem;
	VkDescriptorPool pool;
	VkDescriptorSet descSet;
	VkCommandBuffer cmdBuf;
	VkCommandPool cmdPool;
	VkFence fence;
	VkBool32 initialized;
} ComputeContext;

typedef struct
{
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VkQueue computeQueue;
	VkSurfaceKHR surface;
	int graphicsQueueFamily;
	int presentQueueFamily;
	int computeQueueFamily;
	VkPhysicalDeviceProperties deviceProperties;
	bool has_atomic_float;
} VulkanCore;

typedef struct
{
	VkSwapchainKHR swapchain;
	VkImage *images;
	VkImageView *views;
	uint32_t imageCount;
	VkFormat imageFormat;
	VkExtent2D extent;

	VkImage depthImage;
	VkDeviceMemory depthMemory;
	VkImageView depthView;
	VkFormat depthFormat;
} VulkanSwapchain;

typedef struct
{
	VkRenderPass renderPass;
	VkFramebuffer *framebuffers;
	uint32_t imageCount;
} VulkanRenderPass;

typedef struct
{
	VkCommandPool commandPool;
	VkCommandBuffer *commandBuffers;
	VkSemaphore *imageAvailableSemaphores;
	VkSemaphore *renderFinishedSemaphores;
	VkFence *inFlightFences;
	uint32_t currentFrame;
	uint32_t imageCount;
} VulkanCommands;

typedef struct
{
	VkPipeline node;
	VkPipeline edge;
	VkPipeline label;
	VkPipeline ui;
	VkPipeline menu;
	VkPipeline textQuad;
	VkPipeline ray;
	VkPipeline compute_spherical;
	VkPipeline compute_splc;
	VkPipeline compute_criticality;
} Pipelines;

typedef struct
{
	VkDescriptorSetLayout layout;
	VkDescriptorSetLayout compute_layout;
	VkDescriptorSetLayout splc_compute_layout;
	VkDescriptorPool pool;
	VkDescriptorSet *sets;
	VkDescriptorSet *text_quad_sets;
	VkDescriptorSet *node_label_sets;
	VkDescriptorSet *detail_card_sets;
	VkDescriptorPool splc_pool;
	VkDescriptorSet splc_set;
	VkDescriptorSetLayout crit_compute_layout;
	VkDescriptorPool crit_pool;
	VkDescriptorSet crit_set;
} Descriptors;

typedef struct
{
	VkBuffer nodes_buffer;
	VkDeviceMemory nodes_memory;
	VkBuffer edges_buffer;
	VkDeviceMemory edges_memory;
	VkBuffer traffic_buffer;
	VkDeviceMemory traffic_memory;
	VkBuffer level_buffer;
	VkDeviceMemory level_memory;
	igraph_vector_int_t **level_groups;
	int num_levels;
	int current_level;
	double last_level_time;
	float level_interval;
	bool active;
	bool readback_pending;
	VkBuffer max_buffer;
	VkDeviceMemory max_memory;
	VkPipelineLayout pipeline_layout;
	// Edge count the edges_buffer/edges_memory were actually sized for — the
	// deduplicated (IGRAPH_NO_MULTIPLE) SPLC edge count, which is smaller than
	// GraphData.edge_count whenever the graph has multi-edges. Readback must
	// map by this, not graph->edge_count, or it oversteps the buffer.
	uint32_t buffer_edge_count;
} SPLCComputeContext;

// Main-path dynamic-programming buffers. Levels are uploaded once as a single
// permutation and dispatched one renderer tick at a time.
typedef struct
{
	VkBuffer out_nodes_buffer;
	VkDeviceMemory out_nodes_memory;
	VkBuffer out_edges_buffer;
	VkDeviceMemory out_edges_memory;
	VkBuffer in_nodes_buffer;
	VkDeviceMemory in_nodes_memory;
	VkBuffer in_edges_buffer;
	VkDeviceMemory in_edges_memory;
	VkBuffer level_buffer;
	VkDeviceMemory level_memory;
	VkBuffer lnw_buffer;
	VkDeviceMemory lnw_memory;
	VkBuffer lnx_buffer;
	VkDeviceMemory lnx_memory;
	VkBuffer height_buffer;
	VkDeviceMemory height_memory;
	VkBuffer depth_buffer;
	VkDeviceMemory depth_memory;
	VkBuffer display_edges_buffer;
	VkDeviceMemory display_edges_memory;
	VkBuffer display_max_buffer;
	VkDeviceMemory display_max_memory;
	VkBuffer edge_weights_buffer;
	VkDeviceMemory edge_weights_memory;

	VkPipelineLayout pipeline_layout;

	uint32_t *level_offsets; // start index into the level buffer, per level
	uint32_t *level_sizes;	 // node count, per level
	int num_levels;
	uint32_t node_count;
	uint32_t weight_mode;
	bool active;
	bool readback_pending;
	bool selection_run;
	bool selection_ready;
	int *selection_flags;
	int current_level;
	uint32_t stage;
	double last_level_time;
	double level_interval;
	uint32_t graph_edge_count;
} CritComputeContext;

typedef struct
{
	VkBuffer position;
	VkDeviceMemory position_memory;
	VkBuffer attribute;
	VkDeviceMemory attribute_memory;
	VkBuffer staging;
	VkDeviceMemory staging_memory;
	uint32_t count;
	uint32_t capacity;
} NodeBuffers;

typedef enum {
	TRANSITION_SOURCE_NONE = 0,
	TRANSITION_SOURCE_LAYOUT,
	TRANSITION_SOURCE_STREAM,
} TransitionSource;

typedef struct
{
	VkBuffer prev_node_position;
	VkDeviceMemory prev_node_position_memory;
	uint32_t prev_node_count;
	uint32_t prev_node_capacity;
	VkBuffer prev_edge_position;
	VkDeviceMemory prev_edge_position_memory;
	uint32_t prev_edge_vertex_count;
	uint32_t prev_edge_capacity;
	float t;
	float duration;
	bool active;
	TransitionSource owner;
	uint32_t owner_generation;
	bool has_pending;
	TransitionSource pending_source;
	float pending_duration;
	GraphData *pending_graph; // must remain valid/current until promoted; safe because both callers always pass the single persistent app graph and the main loop is single-threaded
} TransitionState;

typedef struct
{
	VkBuffer position;
	VkDeviceMemory position_memory;
	VkBuffer attribute;
	VkDeviceMemory attribute_memory;
	VkBuffer staging;
	VkDeviceMemory staging_memory;
	uint32_t count;
	uint32_t vertex_count;
	uint32_t capacity;
} EdgeBuffers;

typedef struct
{
	VkBuffer quad_vertex;
	VkDeviceMemory quad_vertex_memory;
	VkBuffer quad_index;
	VkDeviceMemory quad_index_memory;
	VkBuffer instance;
	VkDeviceMemory instance_memory;
	uint32_t node_count;
	uint32_t quad_index_count;
	VkBuffer text_quad_instance;
	VkDeviceMemory text_quad_instance_memory;
	uint32_t text_quad_instance_count;
	TextAtlas text_atlas;
} MenuBuffers;

typedef struct
{
	TextAtlas atlas;
	VkBuffer instance;
	VkDeviceMemory instance_memory;
	uint32_t count;
	uint32_t capacity;
	igraph_bh_tree_t tree;
	bool tree_needs_rebuild;
	vec3 camera_pos;
	int selected_node;
	bool cache_valid;
} LabelBuffers;

typedef struct
{
	TextAtlas atlas;
	VkBuffer instance;
	VkDeviceMemory instance_memory;
	bool visible;
	NodeLabelInstance instance_data;
	int node; // node index the detail card was built for, -1 = none
} DetailCard;

typedef struct Renderer
{
	GLFWwindow *window;

	VulkanCore core;
	VulkanSwapchain swapchain;
	VulkanRenderPass renderPass;
	VulkanCommands commands;

	VkRenderPass renderPassXR; // XR render pass (if format differs)
	VkFormat xrFormat;		   // XR swapchain format (may differ from desktop)
	Descriptors descriptors;
	VkPipelineLayout pipelineLayout;
	Pipelines pipelines;

	EdgeRoutingMode currentRoutingMode;

	VkPipelineLayout computePipelineLayout;

	VkFramebuffer **xrFramebuffers; // [view_index][image_index]
	uint32_t *xrFramebufferImageCount;

	VkBuffer nodeVertexBuffer;
	VkDeviceMemory nodeVertexBufferMemory;

	UniformBuffers ubo;
	AnimStateBuffers anim;
	Texture texture;

	// XR-specific depth buffers (per view, separate from desktop)
	uint32_t xr_view_count;
	VkImage *xrDepthImages;				  // [view_index]
	VkDeviceMemory *xrDepthImageMemories; // [view_index]
	VkImageView *xrDepthImageViews;		  // [view_index]

	NodeBuffers node;

	EdgeBuffers edge;

	TransitionState transition;

	bool needsAttributeUpload;

	VkBuffer labelVertexBuffer;
	VkDeviceMemory labelVertexBufferMemory;

	// Visibility toggles
	bool showNodes;
	bool showEdges;
	bool showUI;
	bool framebufferResized;
	float layoutScale;

	// UI
	VkBuffer uiBgVertexBuffer;
	VkDeviceMemory uiBgVertexBufferMemory;
	VkBuffer uiBgInstanceBuffer;
	VkDeviceMemory uiBgInstanceBufferMemory;
	VkBuffer uiTextInstanceBuffer;
	VkDeviceMemory uiTextInstanceBufferMemory;
	uint32_t uiTextCharCount;

	MenuBuffers menu;

	LabelBuffers label;

	DetailCard detail;

	VkBuffer rayVertexBuffer;
	VkDeviceMemory rayVertexBufferMemory;
	uint32_t rayVertexCount;

	// Ring-buffered sync for graph updates
	VkFence graphUpdateFences[GRAPH_UPDATE_RING_SIZE];
	uint32_t graphUpdateRingIndex;

	// Persistent compute context
	ComputeContext computeCtx;

	// SPLC (Search Path Link Count) animation
	SPLCComputeContext splc;

	// Generalised criticality / baskets of nodes (arXiv:2512.12355 §2.5)
	CritComputeContext crit;

	// BCGL (Binary Classification-Based Graph Layout) compute context
	BCGLComputeContext bcgl_ctx;
} Renderer;

#endif // VULKAN_TYPES_H
