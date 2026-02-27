#ifndef RENDERER_H
#define RENDERER_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include "graph/loader.h"
#include "vulkan/polyhedron.h"

typedef enum {
	ROUTING_MODE_STRAIGHT = 0,
	ROUTING_MODE_SPHERICAL_PCB = 1,
	ROUTING_MODE_3D_HUB_SPOKE = 2,
	ROUTING_MODE_3D_VOXEL = 3
} EdgeRoutingMode;

typedef struct {
	mat4 model;
	mat4 view;
	mat4 proj;
} UniformBufferObject;

typedef struct {
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
	VkPipeline nodeEdgePipeline; // New pipeline for node edges
	VkPipeline spherePipeline;	 // Pipeline for semi-transparent spheres
	VkPipeline edgePipeline;
	VkPipeline labelPipeline;
	VkPipeline uiPipeline;

	EdgeRoutingMode currentRoutingMode;

	VkDescriptorSetLayout computeDescriptorSetLayout;
	VkPipelineLayout computePipelineLayout;

	VkPipeline computeSphericalPipeline;
	VkPipeline computeHubSpokePipeline;

	VkFramebuffer *framebuffers;
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

	struct {
		uint32_t count;
		uint32_t firstInstance;
	} platonicDrawCalls[PLATONIC_COUNT];

	VkBuffer *uniformBuffers;
	VkDeviceMemory *uniformBuffersMemory;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet *descriptorSets;
	uint32_t polyhedronIndexCount;
	UniformBufferObject ubo;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	VkBuffer instanceBuffer;
	VkDeviceMemory instanceBufferMemory;
	uint32_t nodeCount;

	VkBuffer edgeVertexBuffer;
	VkDeviceMemory edgeVertexBufferMemory;
	uint32_t edgeCount;
	uint32_t edgeVertexCount;

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
	VkBuffer uiTextInstanceBuffer;
	VkDeviceMemory uiTextInstanceBufferMemory;
	uint32_t uiTextCharCount;

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

int renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph);
void renderer_cleanup(Renderer *r);
void renderer_draw_frame(Renderer *r);
void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up);
void renderer_update_graph(Renderer *r, GraphData *graph);
void renderer_update_ui(Renderer *r, const char *text);

#endif
