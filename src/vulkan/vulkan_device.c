#include "vulkan/vulkan_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vulkan/utils.h"
#include "xr/openxr_context.h"

// Validation layers for debug builds
#ifdef NDEBUG
static const char *VALIDATION_LAYERS[] = {NULL};
static const int VALIDATION_LAYER_COUNT = 0;
#else
static const char *VALIDATION_LAYERS[] = {"VK_LAYER_KHRONOS_validation"};
static const int VALIDATION_LAYER_COUNT = 1;
#endif

// Required device extensions
static const char *BASE_DEVICE_EXTENSIONS[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static const int BASE_DEVICE_EXTENSION_COUNT = 1;

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
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
		if (presentSupport)
			info.presentFamily = i;

		if (info.graphicsFamily != -1 && info.presentFamily != -1)
			break;
	}
	free(queueFamilies);
	return info;
}

void vulkan_device_create(VulkanCore *core, GLFWwindow *window, XrContext *xr)
{
	core->instance = VK_NULL_HANDLE;
	core->device = VK_NULL_HANDLE;
	core->physicalDevice = VK_NULL_HANDLE;
	core->graphicsQueue = VK_NULL_HANDLE;
	core->presentQueue = VK_NULL_HANDLE;
	core->surface = VK_NULL_HANDLE;

	// Query available extensions
	uint32_t availableExtCount = 0;
	vkEnumerateInstanceExtensionProperties(NULL, &availableExtCount, NULL);
	VkExtensionProperties *availableExts = malloc(sizeof(VkExtensionProperties) * availableExtCount);
	vkEnumerateInstanceExtensionProperties(NULL, &availableExtCount, availableExts);

	// Instance Extensions
	uint32_t glfwExtCount = 0;
	const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

	const char *instExts[64];
	uint32_t instExtCount = 0;
	char *instStrdup[64];
	uint32_t instStrdupCount = 0;

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
			instExts[instExtCount++] = glfwExts[i];
		} else {
			fprintf(stderr, "[Vulkan] Warning: GLFW requested extension %s not supported by loader.\n", glfwExts[i]);
		}
	}

	if (xr) {
		char xrInstExts[4096];
		uint32_t xrInstExtsSize = sizeof(xrInstExts);
		xr_context_get_vulkan_instance_extensions(xr, xrInstExts, &xrInstExtsSize);

		char *token = strtok(xrInstExts, " ");
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
				instExts[instExtCount++] = dup;
				instStrdup[instStrdupCount++] = dup;
			} else {
				fprintf(stderr, "[Vulkan] Warning: XR requested extension %s not supported by loader.\n", token);
			}
			token = strtok(NULL, " ");
		}
	}

	bool hasPortability = false;
	for (uint32_t i = 0; i < availableExtCount; i++) {
		if (strcmp(availableExts[i].extensionName, "VK_KHR_portability_enumeration") == 0) {
			hasPortability = true;
			break;
		}
	}
	if (hasPortability) {
		bool alreadyAdded = false;
		for (uint32_t i = 0; i < instExtCount; i++) {
			if (strcmp(instExts[i], "VK_KHR_portability_enumeration") == 0) {
				alreadyAdded = true;
				break;
			}
		}
		if (!alreadyAdded) {
			printf("[Vulkan] Enabling Portability Extension\n");
			instExts[instExtCount++] = "VK_KHR_portability_enumeration";
		}
	}
	free(availableExts);

	// Query available layers
	uint32_t availableLayerCount = 0;
	vkEnumerateInstanceLayerProperties(&availableLayerCount, NULL);
	VkLayerProperties *availableLayers = malloc(sizeof(VkLayerProperties) * availableLayerCount);
	vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers);

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

	VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "igraph-vlk", .applicationVersion = VK_MAKE_VERSION(1, 0, 0), .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1, 0, 0), .apiVersion = VK_API_VERSION_1_0};

	VkInstanceCreateInfo instInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo, .ppEnabledExtensionNames = instExts, .enabledExtensionCount = instExtCount, .ppEnabledLayerNames = (enabledLayerCount > 0) ? enabledLayers : NULL, .enabledLayerCount = enabledLayerCount};

	if (hasPortability) {
		instInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	VK_CHECK(vkCreateInstance(&instInfo, NULL, &core->instance), "Failed to create Vulkan instance");

	for (uint32_t i = 0; i < instStrdupCount; i++)
		free(instStrdup[i]);

	VK_CHECK(glfwCreateWindowSurface(core->instance, window, NULL, &core->surface), "Failed to create window surface");

	if (xr) {
		core->physicalDevice = xr_context_get_vulkan_graphics_device(xr, core->instance);
	} else {
		uint32_t devCount = 0;
		vkEnumeratePhysicalDevices(core->instance, &devCount, NULL);
		if (devCount == 0)
			exit_with_error("Failed to find GPUs with Vulkan support");
		VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * devCount);
		vkEnumeratePhysicalDevices(core->instance, &devCount, devices);

		int bestScore = -1;
		for (uint32_t i = 0; i < devCount; i++) {
			int score = rate_device_suitability(devices[i]);
			if (score > bestScore) {
				bestScore = score;
				core->physicalDevice = devices[i];
			}
		}
		free(devices);
	}

	if (core->physicalDevice == VK_NULL_HANDLE)
		exit_with_error("Failed to find a suitable GPU");

	VkQueueFamilyInfo qFam = find_queue_families(core->physicalDevice, core->surface);
	if (qFam.graphicsFamily == -1 || qFam.presentFamily == -1)
		exit_with_error("Failed to find required queue families");

	core->graphicsQueueFamily = qFam.graphicsFamily;
	core->presentQueueFamily = qFam.presentFamily;

	float qPrio = 1.0f;
	VkDeviceQueueCreateInfo qInfos[2];
	uint32_t qInfoCount = 0;
	qInfos[qInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qFam.graphicsFamily, .queueCount = 1, .pQueuePriorities = &qPrio};
	if (qFam.graphicsFamily != qFam.presentFamily) {
		qInfos[qInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qFam.presentFamily, .queueCount = 1, .pQueuePriorities = &qPrio};
	}

	const char *devExts[64];
	uint32_t devExtCount = 0;
	char *devStrdup[64];
	uint32_t devStrdupCount = 0;
	for (int i = 0; i < BASE_DEVICE_EXTENSION_COUNT; i++)
		devExts[devExtCount++] = BASE_DEVICE_EXTENSIONS[i];

	if (xr) {
		char xrDevExts[4096];
		uint32_t xrDevExtsSize = sizeof(xrDevExts);
		xr_context_get_vulkan_device_extensions(xr, xrDevExts, &xrDevExtsSize);

		char *token = strtok(xrDevExts, " ");
		while (token) {
			char *dup = strdup(token);
			devExts[devExtCount++] = dup;
			devStrdup[devStrdupCount++] = dup;
			token = strtok(NULL, " ");
		}
	}

	VkPhysicalDeviceFeatures devFeat = {.geometryShader = VK_TRUE};
	VkDeviceCreateInfo devInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = qInfoCount, .pQueueCreateInfos = qInfos, .enabledExtensionCount = devExtCount, .ppEnabledExtensionNames = devExts, .pEnabledFeatures = &devFeat, .ppEnabledLayerNames = (enabledLayerCount > 0) ? enabledLayers : NULL, .enabledLayerCount = enabledLayerCount};

	VK_CHECK(vkCreateDevice(core->physicalDevice, &devInfo, NULL, &core->device), "Failed to create logical device");

	for (uint32_t i = 0; i < devStrdupCount; i++)
		free(devStrdup[i]);

	vkGetDeviceQueue(core->device, qFam.graphicsFamily, 0, &core->graphicsQueue);
	vkGetDeviceQueue(core->device, qFam.presentFamily, 0, &core->presentQueue);
}

void vulkan_device_destroy(VulkanCore *core)
{
	if (core->device != VK_NULL_HANDLE)
		vkDestroyDevice(core->device, NULL);
	if (core->surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(core->instance, core->surface, NULL);
	if (core->instance != VK_NULL_HANDLE)
		vkDestroyInstance(core->instance, NULL);
}
