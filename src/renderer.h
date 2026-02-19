#ifndef RENDERER_H
#define RENDERER_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include "graph_loader.h"

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
} UniformBufferObject;

typedef struct {
    GLFWwindow* window;
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    uint32_t swapchainImageCount;
    VkImage* swapchainImages;
    VkImageView* swapchainImageViews;
    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline edgePipeline;
    VkFramebuffer* framebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;
    VkSemaphore* imageAvailableSemaphores;
    VkSemaphore* renderFinishedSemaphores;
    VkFence* inFlightFences;
    uint32_t currentFrame;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    VkBuffer* uniformBuffers;
    VkDeviceMemory* uniformBuffersMemory;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet* descriptorSets;
    uint32_t sphereIndexCount;
    UniformBufferObject ubo;
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;

    // Graph data
    VkBuffer instanceBuffer;
    VkDeviceMemory instanceBufferMemory;
    uint32_t nodeCount;

    VkBuffer edgeVertexBuffer;
    VkDeviceMemory edgeVertexBufferMemory;
    uint32_t edgeCount;
} Renderer;

int renderer_init(Renderer* r, GLFWwindow* window, GraphData* graph);
void renderer_cleanup(Renderer* r);
void renderer_draw_frame(Renderer* r);
void renderer_update_view(Renderer* r, vec3 pos, vec3 front, vec3 up);

#endif
