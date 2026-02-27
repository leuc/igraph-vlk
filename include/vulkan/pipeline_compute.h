#ifndef PIPELINE_COMPUTE_H
#define PIPELINE_COMPUTE_H

#include <vulkan/vulkan.h>

#include "vulkan/vulkan_device.h"

// Forward declare Renderer to access needed fields
typedef struct Renderer Renderer;

void pipeline_compute_create(Renderer *r, VulkanCore *core);

#endif // PIPELINE_COMPUTE_H
