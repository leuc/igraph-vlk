#ifndef PIPELINE_GRAPHICS_H
#define PIPELINE_GRAPHICS_H

#include <vulkan/vulkan.h>

#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_render_pass.h"
#include "vulkan/vulkan_swapchain.h"

// Forward declare Renderer to access needed fields
typedef struct Renderer Renderer;

void pipeline_graphics_create(Renderer *r, VulkanCore *core,
							   VulkanSwapchain *swap, VkRenderPass renderPass);

#endif // PIPELINE_GRAPHICS_H
