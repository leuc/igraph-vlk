#ifndef VULKAN_TYPES_H
#define VULKAN_TYPES_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

#include "graph/graph_types.h"
#include "vulkan/polyhedron.h"

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
	float animation_progress;
	int animation_direction;
	int is_animating;
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
	vec3 nodePos;
	vec4 charRect;
	vec4 charUV;
	vec3 right;
	vec3 up;
	float selected;
} LabelInstance;

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
	VkPipeline graphicsPipeline;
	VkPipeline spherePipeline; // Pipeline for semi-transparent spheres
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

	VkBuffer labelStagingBuffer;
	VkDeviceMemory labelStagingBufferMemory;
	VkBuffer labelVertexBuffer;
	VkDeviceMemory labelVertexBufferMemory;
	VkBuffer labelInstanceBuffer;
	VkDeviceMemory labelInstanceBufferMemory;
	uint32_t labelCharCount;

	// Visibility toggles
	bool showLabels;
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
	VkBuffer menuTextInstanceBuffer;
	VkDeviceMemory menuTextInstanceBufferMemory;
	uint32_t menuTextCharCount;
	uint32_t menuNodeCount;
	uint32_t menuQuadIndexCount;
	VkPipeline menuPipeline; // Instanced menu rendering pipeline
	VkPipeline rayPipeline;
	VkBuffer rayVertexBuffer;
	VkDeviceMemory rayVertexBufferMemory;
	uint32_t rayVertexCount;

	// Numeric Input Widget (world-space)
	VkBuffer numericQuadVertexBuffer;
	VkDeviceMemory numericQuadVertexBufferMemory;
	VkBuffer numericQuadIndexBuffer;
	VkDeviceMemory numericQuadIndexBufferMemory;
	VkBuffer numericInstanceBuffer;
	VkDeviceMemory numericInstanceBufferMemory;
	uint32_t numericInstanceCount;
	uint32_t numericQuadIndexCount;

	// Numeric widget value string (HUD display)
	char numericValueString[32];
	bool showNumericValue;

	// App context pointer for state checking
	struct AppContext *app_ctx_ptr;

	// Ring-buffered sync for graph updates
	VkFence graphUpdateFences[GRAPH_UPDATE_RING_SIZE];
	uint32_t graphUpdateRingIndex;

	// Persistent compute context
	ComputeContext computeCtx;

	// Layered Spheres (Transparent)
	VkBuffer sphereVertexBuffer;
	VkDeviceMemory sphereVertexBufferMemory;
	VkBuffer sphereIndexBuffer;
	VkDeviceMemory sphereIndexBufferMemory;
	uint32_t *sphereIndexCounts;  // Array of index counts per sphere
	uint32_t *sphereIndexOffsets; // Array of offsets into the index buffer
	uint32_t numSpheres;		  // Number of spheres to draw
	bool showSpheres;			  // Toggle
} Renderer;

#endif // VULKAN_TYPES_H
