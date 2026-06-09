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

typedef struct
{
	int graphicsFamily;
	int presentFamily;
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
	VkSurfaceKHR surface;
	int graphicsQueueFamily;
	int presentQueueFamily;
	VkPhysicalDeviceProperties deviceProperties;
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

typedef struct Renderer
{
	GLFWwindow *window;

	VulkanCore core;
	VulkanSwapchain swapchain;
	VulkanRenderPass renderPass;
	VulkanCommands commands;

	VkRenderPass renderPassXR; // XR render pass (if format differs)
	VkFormat xrFormat;		   // XR swapchain format (may differ from desktop)
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline nodePipeline;
	VkPipeline edgePipeline;
	VkPipeline labelPipeline;
	VkPipeline uiPipeline;

	EdgeRoutingMode currentRoutingMode;

	VkDescriptorSetLayout computeDescriptorSetLayout;
	VkPipelineLayout computePipelineLayout;

	VkPipeline computeSphericalPipeline;

	VkFramebuffer **xrFramebuffers; // [view_index][image_index]
	uint32_t *xrFramebufferImageCount;

	VkBuffer nodeVertexBuffer;
	VkDeviceMemory nodeVertexBufferMemory;

	VkBuffer *uniformBuffers;
	VkDeviceMemory *uniformBuffersMemory;
	void *uboMapped[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	VkDescriptorPool descriptorPool;
	VkDescriptorSet *descriptorSets;
	UniformBufferObject ubo;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	// XR-specific depth buffers (per view, separate from desktop)
	uint32_t xr_view_count;
	VkImage *xrDepthImages;				  // [view_index]
	VkDeviceMemory *xrDepthImageMemories; // [view_index]
	VkImageView *xrDepthImageViews;		  // [view_index]

	VkBuffer nodePositionBuffer;
	VkDeviceMemory nodePositionMemory;
	VkBuffer nodeAttributeBuffer;
	VkDeviceMemory nodeAttributeMemory;
	VkBuffer nodeAttributeStagingBuffer;
	VkDeviceMemory nodeAttributeStagingMemory;
	uint32_t nodeCount;
	uint32_t nodeCapacity;

	VkBuffer edgePositionBuffer;
	VkDeviceMemory edgePositionMemory;
	VkBuffer edgeAttributeBuffer;
	VkDeviceMemory edgeAttributeMemory;
	VkBuffer edgeAttributeStagingBuffer;
	VkDeviceMemory edgeAttributeStagingMemory;
	uint32_t edgeCount;
	uint32_t edgeVertexCount;
	uint32_t edgeCapacity;

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

	// 3D Spherical Menu
	VkBuffer menuQuadVertexBuffer;
	VkDeviceMemory menuQuadVertexBufferMemory;
	VkBuffer menuQuadIndexBuffer;
	VkDeviceMemory menuQuadIndexBufferMemory;
	VkBuffer menuInstanceBuffer;
	VkDeviceMemory menuInstanceBufferMemory;
	uint32_t menuNodeCount;
	uint32_t menuQuadIndexCount;
	VkPipeline menuPipeline; // Instanced menu rendering pipeline

	// Generic text quad pipeline
	VkPipeline textQuadPipeline;
	VkBuffer textQuadInstanceBuffer;
	VkDeviceMemory textQuadInstanceBufferMemory;
	uint32_t textQuadInstanceCount;
	TextAtlas menuTextAtlas;
	VkDescriptorSet *textQuadDescriptorSets;

	// Node Labels (LOD)
	TextAtlas nodeTextAtlas;
	VkDescriptorSet *nodeLabelDescSets;
	VkBuffer nodeLabelInstanceBuffer;
	VkDeviceMemory nodeLabelInstanceBufferMemory;
	uint32_t nodeLabelInstanceCount;
	uint32_t nodeLabelCapacity;
	igraph_bh_tree_t labelTree;
	bool labelTreeNeedsRebuild;
	vec3 labelCameraPos;
	int labelSelectedNode;
	bool labelCacheValid;

	// Detail card (single-instance, dedicated atlas)
	TextAtlas detailCardAtlas;
	VkDescriptorSet *detailCardDescSets;
	VkBuffer detailCardInstanceBuffer;
	VkDeviceMemory detailCardInstanceBufferMemory;
	bool detailCardVisible;
	NodeLabelInstance detailCardInstance;
	int detailCardNode; // node index the detail card was built for, -1 = none

	VkPipeline rayPipeline;
	VkBuffer rayVertexBuffer;
	VkDeviceMemory rayVertexBufferMemory;
	uint32_t rayVertexCount;

	// Ring-buffered sync for graph updates
	VkFence graphUpdateFences[GRAPH_UPDATE_RING_SIZE];
	uint32_t graphUpdateRingIndex;

	// Persistent compute context
	ComputeContext computeCtx;

	// Escape layout (GPU physics simulation)
	VkDescriptorSetLayout escape_physics_desc_layout;
	VkPipelineLayout escape_physics_pipeline_layout;
	VkPipeline escape_physics_pipeline;
	VkDescriptorSetLayout escape_stress_desc_layout;
	VkPipelineLayout escape_stress_pipeline_layout;
	VkPipeline escape_stress_pipeline;
	VkDescriptorPool escape_descriptor_pool;
	VkDescriptorSet escape_physics_desc_set;
	VkDescriptorSet escape_stress_desc_set;
	VkBuffer escape_physics_buffer;
	VkDeviceMemory escape_physics_memory;
	VkBuffer escape_adjacency_buffer;
	VkDeviceMemory escape_adjacency_memory;
	VkBuffer escape_offsets_buffer;
	VkDeviceMemory escape_offsets_memory;
	VkBuffer escape_counts_buffer;
	VkDeviceMemory escape_counts_memory;
	VkBuffer escape_edges_buffer;
	VkDeviceMemory escape_edges_memory;
	VkBuffer escape_global_stress_buffer;
	VkDeviceMemory escape_global_stress_memory;
	VkCommandPool escape_cmd_pool;
	VkCommandBuffer escape_cmd_buf;
	VkFence escape_fence;
	VkBool32 escape_initialized;
	// Escape simulation convergence state
	float escape_previous_stress;
	uint32_t escape_stable_frames;
	bool escape_running;
	int escape_iteration;

	// Ray Tracing (Hybrid Escape Layout)
	bool escape_rt_supported;
	bool escape_rt_initialized;
	float escape_bb_diag;
	uint32_t escape_rt_handle_size;
	uint32_t escape_rt_handle_alignment;
	uint32_t escape_rt_base_alignment;
	uint32_t escape_as_scratch_alignment;

	VkAccelerationStructureKHR escape_blas;
	VkBuffer escape_blas_buffer;
	VkDeviceMemory escape_blas_memory;

	VkAccelerationStructureKHR escape_tlas;
	VkBuffer escape_tlas_buffer;
	VkDeviceMemory escape_tlas_memory;
	VkBuffer escape_tlas_instance_buffer;
	VkDeviceMemory escape_tlas_instance_memory;
	VkBuffer escape_tlas_scratch_buffer;
	VkDeviceMemory escape_tlas_scratch_memory;
	VkDeviceAddress escape_blas_device_address;

	VkPipeline escape_rt_pipeline;
	VkPipelineLayout escape_rt_pipeline_layout;
	VkDescriptorSetLayout escape_rt_desc_layout;
	VkDescriptorSet escape_rt_desc_set;
	VkDescriptorPool escape_rt_descriptor_pool;

	VkBuffer escape_sbt_buffer;
	VkDeviceMemory escape_sbt_memory;
	VkStridedDeviceAddressRegionKHR escape_sbt_raygen;
	VkStridedDeviceAddressRegionKHR escape_sbt_miss;
	VkStridedDeviceAddressRegionKHR escape_sbt_hit;
	VkStridedDeviceAddressRegionKHR escape_sbt_callable;

	// SPLC (Search Path Link Count) animation
	VkBuffer splc_nodes_buffer;
	VkDeviceMemory splc_nodes_memory;
	VkBuffer splc_edges_buffer;
	VkDeviceMemory splc_edges_memory;
	VkBuffer splc_traffic_buffer;
	VkDeviceMemory splc_traffic_memory;
	VkBuffer splc_level_buffer;
	VkDeviceMemory splc_level_memory;
	igraph_vector_int_t **splc_level_groups;
	int splc_num_levels;
	int splc_current_level;
	double splc_last_level_time;
	float splc_level_interval;
	bool splc_active;
	bool splc_readback_pending;
	VkBuffer splc_max_buffer;
	VkDeviceMemory splc_max_memory;
	VkDescriptorPool splc_descriptor_pool;
	VkDescriptorSet splc_descriptor_set;
	VkPipeline splc_compute_pipeline;
	VkPipelineLayout splc_compute_pipeline_layout;
	VkDescriptorSetLayout splc_compute_descriptor_set_layout;
} Renderer;

#endif // VULKAN_TYPES_H
