#ifndef VULKAN_TYPES_H
#define VULKAN_TYPES_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

#include "graph/graph_types.h"
#include "vulkan/polyhedron.h"
#include "vulkan/text.h"

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_VIEWS 3
#define GRAPH_UPDATE_RING_SIZE 3

typedef enum { ROUTING_MODE_STRAIGHT = 0, ROUTING_MODE_SPHERICAL_PCB = 1 } EdgeRoutingMode;

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
	float glow;
	float selected;
} NodeAttribute;

typedef struct
{
	vec3 pos;
} EdgePosition;

typedef struct
{
	vec3 color;
	float size;
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

	VkBuffer vertexBuffers[PLATONIC_COUNT];
	VkDeviceMemory vertexBufferMemories[PLATONIC_COUNT];
	VkBuffer indexBuffers[PLATONIC_COUNT];
	VkDeviceMemory indexBufferMemories[PLATONIC_COUNT];
	uint32_t platonicIndexCounts[PLATONIC_COUNT];

	struct
	{
		uint32_t count;
		uint32_t firstInstance;
	} platonicDrawCalls[PLATONIC_COUNT];

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
	DistIdxPair *labelSortPairs;
	uint32_t labelSortCapacity;

	VkPipeline rayPipeline;
	VkBuffer rayVertexBuffer;
	VkDeviceMemory rayVertexBufferMemory;
	uint32_t rayVertexCount;

	// Ring-buffered sync for graph updates
	VkFence graphUpdateFences[GRAPH_UPDATE_RING_SIZE];
	uint32_t graphUpdateRingIndex;

	// Persistent compute context
	ComputeContext computeCtx;
} Renderer;

#endif // VULKAN_TYPES_H
