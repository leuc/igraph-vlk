/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/utils.h"

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/app_path.h"

VkResult create_shader_module(VkDevice device, const char *rel, VkShaderModule *shaderModule)
{
	const char *path = app_path_resolve(rel);
	FILE *file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "Failed to open shader: %s\n", path);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint32_t *code = malloc(size);
	if (!code) {
		fclose(file);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	size_t read = fread(code, 1, size, file);
	fclose(file);
	if (read != (size_t)size) {
		free(code);
		fprintf(stderr, "Failed to read shader file: %s\n", path);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	VkShaderModuleCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
	VkResult result = vkCreateShaderModule(device, &createInfo, NULL, shaderModule);
	free(code);
	return result;
}

void exit_with_error(const char *msg)
{
	fprintf(stderr, "Fatal Error: %s\n", msg);
	exit(1);
}
