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

// Required device extensions (always needed)
static const char *REQUIRED_DEVICE_EXTENSIONS[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static const int REQUIRED_DEVICE_EXTENSION_COUNT = 1;

// Optional device extensions (enabled if supported)
static const char *OPTIONAL_DEVICE_EXTENSIONS[] = {VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME};
static const int OPTIONAL_DEVICE_EXTENSION_COUNT = 2;

static int rate_device_suitability(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(device, &props);

	int score = 0;
	if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		score += 1000;
	score += props.limits.maxImageDimension2D;
	return score;
}

static VkQueueFamilyInfo find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
	VkQueueFamilyInfo info = {.graphicsFamily = -1, .presentFamily = -1, .computeFamily = -1};

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

	VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

	for (uint32_t i = 0; i < queueFamilyCount; i++) {
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			info.graphicsFamily = i;

		// Prefer a dedicated compute family (compute without graphics)
		if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && info.computeFamily == -1)
			info.computeFamily = i;

		// Fallback: any family with compute bit
		if (info.computeFamily == -1 && (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
			info.computeFamily = i;

		VkBool32 presentSupport = false;
		VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport), "Failed to query physical device surface support");
		if (presentSupport)
			info.presentFamily = i;

		if (info.graphicsFamily != -1 && info.presentFamily != -1 && info.computeFamily != -1)
			break;
	}
	free(queueFamilies);
	return info;
}

static bool has_device_extension(VkPhysicalDevice device, const char *name)
{
	uint32_t count = 0;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL), "Failed to enumerate device extension properties (count)");
	VkExtensionProperties *exts = malloc(sizeof(VkExtensionProperties) * count);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(device, NULL, &count, exts), "Failed to enumerate device extension properties");
	bool found = false;
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(exts[i].extensionName, name) == 0) {
			found = true;
			break;
		}
	}
	free(exts);
	return found;
}

void vulkan_device_create(VulkanCore *core, GLFWwindow *window, void *xr)
{
	core->instance = VK_NULL_HANDLE;
	core->device = VK_NULL_HANDLE;
	core->physicalDevice = VK_NULL_HANDLE;
	core->graphicsQueue = VK_NULL_HANDLE;
	core->presentQueue = VK_NULL_HANDLE;
	core->surface = VK_NULL_HANDLE;
	core->has_atomic_float = false;

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

	VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "igraph-vlk", .applicationVersion = VK_MAKE_VERSION(1, 0, 0), .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1, 0, 0), .apiVersion = VK_API_VERSION_1_2};

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
	core->computeQueueFamily = queueFamilyInfo.computeFamily;

	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfos[3];
	uint32_t queueCreateInfoCount = 0;
	queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilyInfo.graphicsFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};
	if (queueFamilyInfo.graphicsFamily != queueFamilyInfo.presentFamily) {
		queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilyInfo.presentFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};
	}
	if (queueFamilyInfo.computeFamily != queueFamilyInfo.graphicsFamily && queueFamilyInfo.computeFamily != queueFamilyInfo.presentFamily) {
		queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamilyInfo.computeFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};
	}

	// Build device extension list
	const char *deviceExtensions[64];
	uint32_t deviceExtensionCount = 0;
	char *deviceExtensionStrdup[64];
	uint32_t deviceExtensionStrdupCount = 0;

	// Add required extensions (must be supported)
	for (int i = 0; i < REQUIRED_DEVICE_EXTENSION_COUNT; i++) {
		if (has_device_extension(core->physicalDevice, REQUIRED_DEVICE_EXTENSIONS[i])) {
			deviceExtensions[deviceExtensionCount++] = REQUIRED_DEVICE_EXTENSIONS[i];
		} else {
			fprintf(stderr, "[Vulkan] FATAL: Required device extension %s not supported.\n", REQUIRED_DEVICE_EXTENSIONS[i]);
			exit_with_error("Missing required Vulkan device extension");
		}
	}

	// Add optional extensions if supported
	for (int i = 0; i < OPTIONAL_DEVICE_EXTENSION_COUNT; i++) {
		if (has_device_extension(core->physicalDevice, OPTIONAL_DEVICE_EXTENSIONS[i])) {
			printf("[Vulkan] Enabling optional device extension: %s\n", OPTIONAL_DEVICE_EXTENSIONS[i]);
			deviceExtensions[deviceExtensionCount++] = OPTIONAL_DEVICE_EXTENSIONS[i];
			if (strcmp(OPTIONAL_DEVICE_EXTENSIONS[i], VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) == 0)
				core->has_atomic_float = true;
		} else {
			printf("[Vulkan] Optional device extension %s not supported, skipping.\n", OPTIONAL_DEVICE_EXTENSIONS[i]);
		}
	}

	// Add VK_KHR_portability_subset if both portability enumeration and the subset extension are available
	if (hasPortability && has_device_extension(core->physicalDevice, "VK_KHR_portability_subset")) {
		bool alreadyAdded = false;
		for (uint32_t i = 0; i < deviceExtensionCount; i++) {
			if (strcmp(deviceExtensions[i], "VK_KHR_portability_subset") == 0) {
				alreadyAdded = true;
				break;
			}
		}
		if (!alreadyAdded) {
			printf("[Vulkan] Enabling portability subset extension\n");
			deviceExtensions[deviceExtensionCount++] = "VK_KHR_portability_subset";
		}
	}

#ifdef USE_OPENXR
	if (xr) {
		XrContext *xr_ctx = (XrContext *)xr;
		char xrDeviceExtensions[4096];
		uint32_t xrDeviceExtensionsSize = sizeof(xrDeviceExtensions);
		xr_context_get_vulkan_device_extensions(xr_ctx, xrDeviceExtensions, &xrDeviceExtensionsSize);

		char *token = strtok(xrDeviceExtensions, " ");
		while (token) {
			bool alreadyAdded = false;
			for (uint32_t i = 0; i < deviceExtensionCount; i++) {
				if (strcmp(deviceExtensions[i], token) == 0) {
					alreadyAdded = true;
					break;
				}
			}
			if (!alreadyAdded) {
				char *dup = strdup(token);
				deviceExtensions[deviceExtensionCount++] = dup;
				deviceExtensionStrdup[deviceExtensionStrdupCount++] = dup;
			}
			token = strtok(NULL, " ");
		}
	}
#endif

	// Build device feature chain
	VkPhysicalDeviceFeatures deviceFeatures = {0};

	// Vulkan 1.2 features (core, no extension needed)
	VkPhysicalDeviceVulkan12Features vulkan12Features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = NULL,
		.descriptorIndexing = VK_TRUE,
		.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
	};

	void **nextPtr = &vulkan12Features.pNext;

	// Optional: atomic float features
	VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures = {0};
	if (core->has_atomic_float) {
		atomicFloatFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
		atomicFloatFeatures.pNext = NULL;
		atomicFloatFeatures.shaderBufferFloat32Atomics = VK_TRUE;
		atomicFloatFeatures.shaderBufferFloat32AtomicAdd = VK_TRUE;
		*nextPtr = &atomicFloatFeatures;
		nextPtr = &atomicFloatFeatures.pNext;
	}

	VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &vulkan12Features, .queueCreateInfoCount = queueCreateInfoCount, .pQueueCreateInfos = queueCreateInfos, .enabledExtensionCount = deviceExtensionCount, .ppEnabledExtensionNames = deviceExtensions, .pEnabledFeatures = &deviceFeatures, .ppEnabledLayerNames = (enabledLayerCount > 0) ? enabledLayers : NULL, .enabledLayerCount = enabledLayerCount};

	VK_CHECK(vkCreateDevice(core->physicalDevice, &deviceInfo, NULL, &core->device), "Failed to create logical device");

	for (uint32_t i = 0; i < deviceExtensionStrdupCount; i++)
		free(deviceExtensionStrdup[i]);

	vkGetDeviceQueue(core->device, queueFamilyInfo.graphicsFamily, 0, &core->graphicsQueue);
	vkGetDeviceQueue(core->device, queueFamilyInfo.presentFamily, 0, &core->presentQueue);

	// Compute queue: use dedicated compute family if available, otherwise fallback to graphics
	if (queueFamilyInfo.computeFamily >= 0) {
		vkGetDeviceQueue(core->device, queueFamilyInfo.computeFamily, 0, &core->computeQueue);
	} else {
		core->computeQueue = core->graphicsQueue;
	}

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
