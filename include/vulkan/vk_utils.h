#ifndef VK_UTILS_H
#define VK_UTILS_H

#include <vulkan/vulkan.h>

VkResult create_shader_module(VkDevice device, const char *path, VkShaderModule *shaderModule);
void exit_with_error(const char *msg);

#endif
