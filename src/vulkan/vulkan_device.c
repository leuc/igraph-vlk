#include "vulkan/vulkan_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkan/utils.h"

// Validation layers for debug builds
#ifdef NDEBUG
static const char *VALIDATION_LAYERS[] = {};
static const int VALIDATION_LAYER_COUNT = 0;
#else
static const char *VALIDATION_LAYERS[] = {"VK_LAYER_KHRONOS_validation"};
static const int VALIDATION_LAYER_COUNT = 1;
#endif

// Required device extensions
static const char *DEVICE_EXTENSIONS[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static const int DEVICE_EXTENSION_COUNT = 1;

static int rate_device_suitability(VkPhysicalDevice device) {
	VkPhysicalDeviceProperties props;
	VkPhysicalDeviceFeatures features;
	vkGetPhysicalDeviceProperties(device, &props);
	vkGetPhysicalDeviceFeatures(device, &features);

	int score = 0;
	// Discrete GPUs have a significant performance advantage
	if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		score += 1000;
	// Maximum possible size of textures affects graphics quality
	score += props.limits.maxImageDimension2D;
	// Application can't function without geometry shaders
	if (!features.geometryShader)
		score = 0;
	return score;
}

static int check_device_extension_support(VkPhysicalDevice device) {
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);

	VkExtensionProperties *available =
		malloc(sizeof(VkExtensionProperties) * extensionCount);
	vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount,
										  available);

	int found = 0;
	for (int i = 0; i < DEVICE_EXTENSION_COUNT; i++) {
		for (uint32_t j = 0; j < extensionCount; j++) {
			if (strcmp(DEVICE_EXTENSIONS[i], available[j].extensionName) ==
				0) {
				found++;
				break;
			}
		}
	}
	free(available);
	return found == DEVICE_EXTENSION_COUNT;
}

static VkQueueFamilyInfo find_queue_families(VkPhysicalDevice device,
											  VkSurfaceKHR surface) {
	VkQueueFamilyInfo info = {.graphicsFamily = -1, .presentFamily = -1};

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

	VkQueueFamilyProperties *queueFamilies =
		malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
											  queueFamilies);

	int i = 0;
	for (queueFamilies; i < queueFamilyCount; i++) {
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

void vulkan_device_create(VulkanCore *core, GLFWwindow *window) {
	// Initialize to NULL for proper cleanup on error
	core->instance = VK_NULL_HANDLE;
	core->device = VK_NULL_HANDLE;
	core->physicalDevice = VK_NULL_HANDLE;
	core->graphicsQueue = VK_NULL_HANDLE;
	core->presentQueue = VK_NULL_HANDLE;

	// Create Vulkan instance
	VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO,
								 .pApplicationName = "igraph-vlk",
								 .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
								 .pEngineName = "No Engine",
								 .engineVersion = VK_MAKE_VERSION(1, 0, 0),
								 .apiVersion = VK_API_VERSION_1_0};

	uint32_t glfwExtensionCount = 0;
	const char **glfwExtensions =
		glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	VkInstanceCreateInfo createInfo = {
		VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		NULL,
		0,
		&appInfo,
		VALIDATION_LAYER_COUNT,
		VALIDATION_LAYERS,
		glfwExtensionCount,
		glfwExtensions};

	VK_CHECK(vkCreateInstance(&createInfo, NULL, &core->instance),
			 "Failed to create Vulkan instance");

	// Create window surface
	VK_CHECK(glfwCreateWindowSurface(core->instance, window, NULL, &core->surface),
			 "Failed to create window surface");

	// Select physical device
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(core->instance, &deviceCount, NULL);
	if (deviceCount == 0)
		exit_with_error("Failed to find GPUs with Vulkan support");

	VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
	vkEnumeratePhysicalDevices(core->instance, &deviceCount, devices);

	int bestScore = 0;
	int bestDeviceIndex = 0;
	for (uint32_t i = 0; i < deviceCount; i++) {
		int score = rate_device_suitability(devices[i]);
		if (score > bestScore) {
			bestScore = score;
			bestDeviceIndex = i;
		}
	}
	core->physicalDevice = devices[bestDeviceIndex];
	free(devices);

	if (core->physicalDevice == VK_NULL_HANDLE)
		exit_with_error("Failed to find a suitable GPU");

	// Check required extensions
	if (!check_device_extension_support(core->physicalDevice))
		exit_with_error("Missing required device extensions");

	// Find queue families
	VkQueueFamilyInfo queueInfo =
		find_queue_families(core->physicalDevice, core->surface);
	if (queueInfo.graphicsFamily == -1 || queueInfo.presentFamily == -1)
		exit_with_error("Failed to find required queue families");

	// Create logical device
	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfos[2] = {{0}};
	queueCreateInfos[0] = (VkDeviceQueueCreateInfo){
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		NULL,
		0,
		queueInfo.graphicsFamily,
		1,
		&queuePriority};
	queueCreateInfos[1] = (VkDeviceQueueCreateInfo){
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		NULL,
		0,
		queueInfo.presentFamily,
		1,
		&queuePriority};

	VkPhysicalDeviceFeatures deviceFeatures = {.geometryShader = VK_TRUE};

	VkDeviceCreateInfo deviceCreateInfo = {
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		NULL,
		0,
		(queueInfo.graphicsFamily == queueInfo.presentFamily) ? 1 : 2,
		queueCreateInfos,
		VALIDATION_LAYER_COUNT,
		VALIDATION_LAYERS,
		DEVICE_EXTENSION_COUNT,
		DEVICE_EXTENSIONS,
		&deviceFeatures};

	VK_CHECK(vkCreateDevice(core->physicalDevice, &deviceCreateInfo, NULL,
							&core->device),
			 "Failed to create logical device");

	vkGetDeviceQueue(core->device, queueInfo.graphicsFamily, 0,
					 &core->graphicsQueue);
	vkGetDeviceQueue(core->device, queueInfo.presentFamily, 0,
					 &core->presentQueue);
}

void vulkan_device_destroy(VulkanCore *core) {
	if (core->device != VK_NULL_HANDLE)
		vkDestroyDevice(core->device, NULL);
	if (core->surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(core->instance, core->surface, NULL);
	if (core->instance != VK_NULL_HANDLE)
		vkDestroyInstance(core->instance, NULL);
}
