#ifndef RENDERER_H
#define RENDERER_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include "graph/graph_types.h"
#include "vulkan/polyhedron.h"

struct AppContext;

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_VIEWS 3
#define GRAPH_UPDATE_RING_SIZE 3

typedef enum { ROUTING_MODE_STRAIGHT = 0, ROUTING_MODE_SPHERICAL_PCB = 1 } EdgeRoutingMode;

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
	GLFWwindow *window;
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice physicalDevice;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	VkExtent2D swapchainExtent;
	uint32_t swapchainImageCount;
	VkImage *swapchainImages;
	VkImageView *swapchainImageViews;
	VkRenderPass renderPass;
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

	VkFramebuffer *framebuffers;
	VkFramebuffer **xrFramebuffers; // [view_index][image_index]
	uint32_t *xrFramebufferImageCount;
	VkCommandPool commandPool;
	VkCommandBuffer *commandBuffers;
	VkSemaphore *imageAvailableSemaphores;
	VkSemaphore *renderFinishedSemaphores;
	VkFence *inFlightFences;
	uint32_t currentFrame;
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
	uint32_t polyhedronIndexCount;
	UniformBufferObject ubo;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;
	VkFormat depthFormat;

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

	// Crosshair (screen-space overlay)
	VkBuffer crosshairVertexBuffer;
	VkDeviceMemory crosshairVertexBufferMemory;
	uint32_t crosshairVertexCount;

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

	// Results display (HUD)
	char resultsMessage[128];
	bool showResultsMessage;

	// App context pointer for state checking
	struct AppContext *app_ctx_ptr;

	// Ring-buffered sync for graph updates
	VkFence graphUpdateFences[GRAPH_UPDATE_RING_SIZE];
	uint32_t graphUpdateRingIndex;

	// Persistent compute context
	ComputeContext computeCtx;

	// Command buffer caching
	VkBool32 cmdBufferValid;
	uint64_t lastSceneHash;

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

#include "xr/openxr_context.h"

int renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, XrContext *xr);
void renderer_create_depth_resources(Renderer *r, uint32_t width, uint32_t height);
void renderer_setup_xr(Renderer *r, XrContext *xr);
void renderer_cleanup(Renderer *r);
void renderer_draw_frame(Renderer *r);
void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index);
void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up);
void renderer_update_graph(Renderer *r, GraphData *graph);
// renderer_update_ui is declared in renderer_ui.h

#endif
