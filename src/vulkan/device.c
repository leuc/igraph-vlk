/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vulkan/utils.h"

#ifdef USE_OPENXR
#include "xr/openxr_context.h"
#endif

// Validation layers for debug builds
#ifdef NDEBUG
static const char *VALIDATION_LAYERS[] = {NULL};
static const int VALIDATION_LAYER_COUNT = 0;
#else
static const char *VALIDATION_LAYERS[] = {"VK_LAYER_KHRONOS_validation"};
static const int VALIDATION_LAYER_COUNT = 1;
#endif

// Required device extensions
static const char *BASE_DEVICE_EXTENSIONS[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME};
static const int BASE_DEVICE_EXTENSION_COUNT = 3;

static int rate_device_suitability(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties props;
	VkPhysicalDeviceFeatures features;
	vkGetPhysicalDeviceProperties(device, &props);
	vkGetPhysicalDeviceFeatures(device, &features);

	int score = 0;
	if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		score += 1000;
	score += props.limits.maxImageDimension2D;
	if (!features.geometryShader)
		score = 0;
	return score;
}

static VkQueueFamilyInfo find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
	VkQueueFamilyInfo info = {.graphicsFamily = -1, .presentFamily = -1};

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

	VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

	for (uint32_t i = 0; i < queueFamilyCount; i++) {
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			info.graphicsFamily = i;

		VkBool32 presentSupport = false;
		VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport), "Failed to query physical device surface support");
		if (presentSupport)
			info.presentFamily = i;

		if (info.graphicsFamily != -1 && info.presentFamily != -1)
			break;
	}
	free(queueFamilies);
	return info;
}

void vulkan_device_create(VulkanCore *core, GLFWwindow *window, void *xr)
{
	core->instance = VK_NULL_HANDLE;
	core->device = VK_NULL_HANDLE;
	core->physicalDevice = VK_NULL_HANDLE;
	core->graphicsQueue = VK_NULL_HANDLE;
	core->presentQueue = VK_NULL_HANDLE;
	core->surface = VK_NULL_HANDLE;

	// Query available extensions
	uint32_t availableExtCount = 0;
	VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &availableExtCount, NULL), "Failed to enumerate instance extension properties (count)");
	VkExtensionProperties *availableExts = malloc(sizeof(VkExtensionProperties) * availableExtCount);
	VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &availableExtCount, availableExts), "Failed to enumerate instance extension properties");

	// Instance Extensions
	uint32_t glfwExtCount = 0;
	const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

	const char *instanceExtensions[64];
	uint32_t instanceExtensionCount = 0;
	char *instanceExtensionStrdup[64];
	uint32_t instanceExtensionStrdupCount = 0;

	for (uint32_t i = 0; i < glfwExtCount; i++) {
		bool supported = false;
		for (uint32_t j = 0; j < availableExtCount; j++) {
			if (strcmp(glfwExts[i], availableExts[j].extensionName) == 0) {
				supported = true;
				break;
			}
		}
		if (supported) {
			printf("[Vulkan] Enabling GLFW Extension: %s\n", glfwExts[i]);
			instanceExtensions[instanceExtensionCount++] = glfwExts[i];
		} else {
			fprintf(stderr, "[Vulkan] Warning: GLFW requested extension %s not supported by loader.\n", glfwExts[i]);
		}
	}

#ifdef USE_OPENXR
	if (xr) {
		XrContext *xr_ctx = (XrContext *)xr;
		char xrInstanceExtensions[4096];
		uint32_t xrInstanceExtensionsSize = sizeof(xrInstanceExtensions);
		xr_context_get_vulkan_instance_extensions(xr_ctx, xrInstanceExtensions, &xrInstanceExtensionsSize);

		char *token = strtok(xrInstanceExtensions, " ");
		while (token) {
			bool supported = false;
			for (uint32_t j = 0; j < availableExtCount; j++) {
				if (strcmp(token, availableExts[j].extensionName) == 0) {
					supported = true;
					break;
				}
			}
			if (supported) {
				printf("[Vulkan] Enabling XR Extension: %s\n", token);
				char *dup = strdup(token);
				instanceExtensions[instanceExtensionCount++] = dup;
				instanceExtensionStrdup[instanceExtensionStrdupCount++] = dup;
			} else {
				fprintf(stderr, "[Vulkan] Warning: XR requested extension %s not supported by loader.\n", token);
			}
			token = strtok(NULL, " ");
		}
	}
#endif

	bool hasPortability = false;
	for (uint32_t i = 0; i < availableExtCount; i++) {
		if (strcmp(availableExts[i].extensionName, "VK_KHR_portability_enumeration") == 0) {
			hasPortability = true;
			break;
		}
	}
	if (hasPortability) {
		bool alreadyAdded = false;
		for (uint32_t i = 0; i < instanceExtensionCount; i++) {
			if (strcmp(instanceExtensions[i], "VK_KHR_portability_enumeration") == 0) {
				alreadyAdded = true;
				break;
			}
		}
		if (!alreadyAdded) {
			printf("[Vulkan] Enabling Portability Extension\n");
			instanceExtensions[instanceExtensionCount++] = "VK_KHR_portability_enumeration";
		}
	}
	free(availableExts);

	// Query available layers
	uint32_t availableLayerCount = 0;
	VK_CHECK(vkEnumerateInstanceLayerProperties(&availableLayerCount, NULL), "Failed to enumerate instance layer properties (count)");
	VkLayerProperties *availableLayers = malloc(sizeof(VkLayerProperties) * availableLayerCount);
	VK_CHECK(vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers), "Failed to enumerate instance layer properties");

	const char *enabledLayers[16];
	uint32_t enabledLayerCount = 0;
	for (int i = 0; i < VALIDATION_LAYER_COUNT; i++) {
		if (!VALIDATION_LAYERS[i])
			continue;
		bool found = false;
		for (uint32_t j = 0; j < availableLayerCount; j++) {
			if (strcmp(VALIDATION_LAYERS[i], availableLayers[j].layerName) == 0) {
				found = true;
				break;
			}
		}
		if (found) {
			enabledLayers[enabledLayerCount++] = VALIDATION_LAYERS[i];
		} else {
			fprintf(stderr, "[Vulkan] Warning: Requested layer %s not found.\n", VALIDATION_LAYERS[i]);
		}
	}
	free(availableLayers);

	VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "igraph-vlk", .applicationVersion = VK_MAKE_VERSION(1, 0, 0), .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1, 0, 0), .apiVersion = VK_API_VERSION_1_1};

	VkInstanceCreateInfo instanceInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo, .ppEnabledExtensionNames = instanceExtensions, .enabledExtensionCount = instanceExtensionCount, .ppEnabledLayerNames = (enabledLayerCount > 0) ? enabledLayers : NULL, .enabledLayerCount = enabledLayerCount};

	if (hasPortability) {
		instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	VK_CHECK(vkCreateInstance(&instanceInfo, NULL, &core->instance), "Failed to create Vulkan instance");

	for (uint32_t i = 0; i < instanceExtensionStrdupCount; i++)
		free(instanceExtensionStrdup[i]);

	VK_CHECK(glfwCreateWindowSurface(core->instance, window, NULL, &core->surface), "Failed to create window surface");

#ifdef USE_OPENXR
	if (xr) {
		XrContext *xr_ctx = (XrContext *)xr;
		core->physicalDevice = xr_context_get_vulkan_graphics_device(xr_ctx, core->instance);
	} else {
#endif
		{
			uint32_t deviceCount = 0;
			VK_CHECK(vkEnumeratePhysicalDevices(core->instance, &deviceCount, NULL), "Failed to enumerate physical devices (count)");
			if (deviceCount == 0)
				exit_with_error("Failed to find GPUs with Vulkan support");
			VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
			VK_CHECK(vkEnumeratePhysicalDevices(core->instance, &deviceCount, devices), "Failed to enumerate physical devices");

			int bestScore = -1;
			for (uint32_t i = 0; i < deviceCount; i++) {
				int score = rate_device_suitability(devices[i]);
				if (score > bestScore) {
					bestScore = score;
					core->physicalDevice = devices[i];
				}
			}
			free(devices);
		}
#ifdef USE_OPENXR
	}
#endif

	if (core->physicalDevice == VK_NULL_HANDLE)
		exit_with_error("Failed to find a suitable GPU");

	VkQueueFamilyInfo queueFamilyInfo = find_queue_families(core->physicalDevice, core->surface);
	if (queueFamilyInfo.graphicsFamily == -1 || queueFamilyInfo.presentFamily == -1)
		exit_with_error("Failed to find required queue families");

	core->graphicsQueueFamily = queueFamilyInfo.graphicsFamily;
	core->presentQueueFamily = queueFamilyInfo.presentFamily;

	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfos[2];
	uint32_t queueCreateInfoCount = 0;
	queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilyInfo.graphicsFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};
	if (queueFamilyInfo.graphicsFamily != queueFamilyInfo.presentFamily) {
		queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilyInfo.presentFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};
	}

	const char *deviceExtensions[64];
	uint32_t deviceExtensionCount = 0;
	char *deviceExtensionStrdup[64];
	uint32_t deviceExtensionStrdupCount = 0;
	for (int i = 0; i < BASE_DEVICE_EXTENSION_COUNT; i++)
		deviceExtensions[deviceExtensionCount++] = BASE_DEVICE_EXTENSIONS[i];

#ifdef USE_OPENXR
	if (xr) {
		XrContext *xr_ctx = (XrContext *)xr;
		char xrDeviceExtensions[4096];
		uint32_t xrDeviceExtensionsSize = sizeof(xrDeviceExtensions);
		xr_context_get_vulkan_device_extensions(xr_ctx, xrDeviceExtensions, &xrDeviceExtensionsSize);

		char *token = strtok(xrDeviceExtensions, " ");
		while (token) {
			char *dup = strdup(token);
			deviceExtensions[deviceExtensionCount++] = dup;
			deviceExtensionStrdup[deviceExtensionStrdupCount++] = dup;
			token = strtok(NULL, " ");
		}
	}
#endif

	VkPhysicalDeviceFeatures deviceFeatures = {.geometryShader = VK_TRUE};
	VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
		.pNext = NULL,
		.shaderBufferFloat32Atomics = VK_TRUE,
		.shaderBufferFloat32AtomicAdd = VK_TRUE,
	};
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descIndexingFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
		.pNext = &atomicFloatFeatures,
		.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
	};
	VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = queueCreateInfoCount, .pQueueCreateInfos = queueCreateInfos, .enabledExtensionCount = deviceExtensionCount, .ppEnabledExtensionNames = deviceExtensions, .pEnabledFeatures = &deviceFeatures, .pNext = &descIndexingFeatures, .ppEnabledLayerNames = (enabledLayerCount > 0) ? enabledLayers : NULL, .enabledLayerCount = enabledLayerCount};

	VK_CHECK(vkCreateDevice(core->physicalDevice, &deviceInfo, NULL, &core->device), "Failed to create logical device");

	for (uint32_t i = 0; i < deviceExtensionStrdupCount; i++)
		free(deviceExtensionStrdup[i]);

	vkGetDeviceQueue(core->device, queueFamilyInfo.graphicsFamily, 0, &core->graphicsQueue);
	vkGetDeviceQueue(core->device, queueFamilyInfo.presentFamily, 0, &core->presentQueue);

	vkGetPhysicalDeviceProperties(core->physicalDevice, &core->deviceProperties);
}

void vulkan_device_destroy(VulkanCore *core)
{
	if (core->device != VK_NULL_HANDLE) {
		vkDestroyDevice(core->device, NULL);
	}
	if (core->surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(core->instance, core->surface, NULL);
	if (core->instance != VK_NULL_HANDLE)
		vkDestroyInstance(core->instance, NULL);
}
