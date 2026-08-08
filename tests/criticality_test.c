/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/criticality_types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define BINDING_COUNT 14
#define WORKGROUP_SIZE 64
#define TOLERANCE 1e-4f

typedef struct
{
	uint32_t strength_max_bits;
	uint32_t _reserved[3];
} EdgeAnimHeader;

typedef struct
{
	float reveal_at;
	float strength;
} EdgeAnim;

typedef struct
{
	uint32_t node_count;
	uint32_t edge_count;
	uint32_t num_levels;
	const CritNode *out_nodes;
	const CritEdge *out_edges;
	const CritNode *in_nodes;
	const CritEdge *in_edges;
	const uint32_t *level_nodes;
	const uint32_t *level_offsets;
	const uint32_t *level_sizes;
} GraphSpec;

typedef struct
{
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t queue_family;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_set;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkBuffer buffers[BINDING_COUNT];
	VkDeviceMemory memories[BINDING_COUNT];
	uint32_t tile_word_count;
} Harness;

enum { BUF_OUT_NODES = 0, BUF_OUT_EDGES, BUF_IN_NODES, BUF_IN_EDGES, BUF_LEVELS, BUF_LNW, BUF_LNX, BUF_HEIGHT, BUF_DEPTH, BUF_EDGE_ANIM, BUF_RESULT, BUF_REACHABILITY, BUF_TOTAL_COUNT_FWD, BUF_TOTAL_COUNT_REV };

#define VK_TRY(expression, message) \
	do { \
		VkResult result = (expression); \
		if (result != VK_SUCCESS) { \
			fprintf(stderr, "%s failed (VkResult %d)\n", message, result); \
			return 1; \
		} \
	} while (0)

static uint32_t float_bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float bits_float(uint32_t bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t filter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
		if ((filter & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	return UINT32_MAX;
}

static int create_buffer(Harness *h, uint32_t binding, VkDeviceSize size, VkBufferUsageFlags usage)
{
	VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_TRY(vkCreateBuffer(h->device, &info, NULL, &h->buffers[binding]), "vkCreateBuffer");
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(h->device, h->buffers[binding], &requirements);
	uint32_t type = find_memory_type(h->physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (type == UINT32_MAX)
		return 1;
	VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = type};
	VK_TRY(vkAllocateMemory(h->device, &allocation, NULL, &h->memories[binding]), "vkAllocateMemory");
	VK_TRY(vkBindBufferMemory(h->device, h->buffers[binding], h->memories[binding], 0), "vkBindBufferMemory");
	return 0;
}

static int upload(Harness *h, uint32_t binding, const void *data, size_t size)
{
	if (size == 0)
		return 0;
	void *mapped = NULL;
	VK_TRY(vkMapMemory(h->device, h->memories[binding], 0, size, 0, &mapped), "vkMapMemory upload");
	memcpy(mapped, data, size);
	vkUnmapMemory(h->device, h->memories[binding]);
	return 0;
}

static int download(Harness *h, uint32_t binding, void *data, size_t size)
{
	void *mapped = NULL;
	VK_TRY(vkMapMemory(h->device, h->memories[binding], 0, size, 0, &mapped), "vkMapMemory download");
	memcpy(data, mapped, size);
	vkUnmapMemory(h->device, h->memories[binding]);
	return 0;
}

static int load_shader(Harness *h, VkShaderModule *shader)
{
	FILE *file = fopen(CRITICALITY_TEST_SHADER_PATH, "rb");
	if (!file)
		return 1;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint32_t *code = malloc((size_t)size);
	if (!code || fread(code, 1, (size_t)size, file) != (size_t)size) {
		free(code);
		fclose(file);
		return 1;
	}
	fclose(file);
	VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)size, .pCode = code};
	VkResult result = vkCreateShaderModule(h->device, &info, NULL, shader);
	free(code);
	VK_TRY(result, "vkCreateShaderModule");
	return 0;
}

static int harness_init(Harness *h)
{
	memset(h, 0, sizeof(*h));
	VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "criticality_test", .apiVersion = VK_API_VERSION_1_1};
	VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &application};
	VK_TRY(vkCreateInstance(&instance_info, NULL, &h->instance), "vkCreateInstance");
	uint32_t physical_count = 0;
	VK_TRY(vkEnumeratePhysicalDevices(h->instance, &physical_count, NULL), "vkEnumeratePhysicalDevices");
	if (physical_count == 0)
		return 1;
	VkPhysicalDevice *physical_devices = malloc(sizeof(*physical_devices) * physical_count);
	if (!physical_devices)
		return 1;
	VK_TRY(vkEnumeratePhysicalDevices(h->instance, &physical_count, physical_devices), "vkEnumeratePhysicalDevices");
	h->physical = physical_devices[0];
	free(physical_devices);
	uint32_t family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(h->physical, &family_count, NULL);
	VkQueueFamilyProperties *families = malloc(sizeof(*families) * family_count);
	if (!families)
		return 1;
	vkGetPhysicalDeviceQueueFamilyProperties(h->physical, &family_count, families);
	h->queue_family = UINT32_MAX;
	for (uint32_t i = 0; i < family_count; i++)
		if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
			h->queue_family = i;
			break;
		}
	free(families);
	if (h->queue_family == UINT32_MAX)
		return 1;
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = h->queue_family, .queueCount = 1, .pQueuePriorities = &priority};
	VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info};
	VK_TRY(vkCreateDevice(h->physical, &device_info, NULL, &h->device), "vkCreateDevice");
	vkGetDeviceQueue(h->device, h->queue_family, 0, &h->queue);
	VkDescriptorSetLayoutBinding bindings[BINDING_COUNT];
	for (uint32_t i = 0; i < BINDING_COUNT; i++)
		bindings[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
	VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = BINDING_COUNT, .pBindings = bindings};
	VK_TRY(vkCreateDescriptorSetLayout(h->device, &layout_info, NULL, &h->set_layout), "vkCreateDescriptorSetLayout");
	VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = sizeof(CritPushConstants)};
	VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &h->set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
	VK_TRY(vkCreatePipelineLayout(h->device, &pipeline_layout_info, NULL, &h->pipeline_layout), "vkCreatePipelineLayout");
	VkShaderModule shader = VK_NULL_HANDLE;
	if (load_shader(h, &shader) != 0)
		return 1;
	VkPipelineShaderStageCreateInfo stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"};
	VkComputePipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = stage, .layout = h->pipeline_layout};
	VkResult pipeline_result = vkCreateComputePipelines(h->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &h->pipeline);
	vkDestroyShaderModule(h->device, shader, NULL);
	VK_TRY(pipeline_result, "vkCreateComputePipelines");
	VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BINDING_COUNT};
	VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
	VK_TRY(vkCreateDescriptorPool(h->device, &pool_info, NULL, &h->descriptor_pool), "vkCreateDescriptorPool");
	VkDescriptorSetAllocateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = h->descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &h->set_layout};
	VK_TRY(vkAllocateDescriptorSets(h->device, &set_info, &h->descriptor_set), "vkAllocateDescriptorSets");
	VkCommandPoolCreateInfo command_pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = h->queue_family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
	VK_TRY(vkCreateCommandPool(h->device, &command_pool_info, NULL, &h->command_pool), "vkCreateCommandPool");
	VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = h->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VK_TRY(vkAllocateCommandBuffers(h->device, &command_info, &h->command_buffer), "vkAllocateCommandBuffers");
	return 0;
}

static void harness_destroy_graph(Harness *h)
{
	for (uint32_t i = 0; i < BINDING_COUNT; i++) {
		if (h->buffers[i])
			vkDestroyBuffer(h->device, h->buffers[i], NULL);
		if (h->memories[i])
			vkFreeMemory(h->device, h->memories[i], NULL);
		h->buffers[i] = VK_NULL_HANDLE;
		h->memories[i] = VK_NULL_HANDLE;
	}
}

static void harness_destroy(Harness *h)
{
	harness_destroy_graph(h);
	if (h->command_pool)
		vkDestroyCommandPool(h->device, h->command_pool, NULL);
	if (h->descriptor_pool)
		vkDestroyDescriptorPool(h->device, h->descriptor_pool, NULL);
	if (h->pipeline)
		vkDestroyPipeline(h->device, h->pipeline, NULL);
	if (h->pipeline_layout)
		vkDestroyPipelineLayout(h->device, h->pipeline_layout, NULL);
	if (h->set_layout)
		vkDestroyDescriptorSetLayout(h->device, h->set_layout, NULL);
	if (h->device)
		vkDestroyDevice(h->device, NULL);
	if (h->instance)
		vkDestroyInstance(h->instance, NULL);
}

static int harness_upload_graph(Harness *h, const GraphSpec *graph, size_t nppc_tile_budget_bytes)
{
	harness_destroy_graph(h);
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(h->physical, &device_properties);
	VkDeviceSize node_size = sizeof(CritNode) * graph->node_count;
	VkDeviceSize edge_size = sizeof(CritEdge) * (graph->edge_count > 0 ? graph->edge_count : 1);
	VkDeviceSize value_size = sizeof(float) * graph->node_count;
	VkDeviceSize animation_size = sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * (graph->edge_count > 0 ? graph->edge_count : 1);
	VkDeviceSize result_size = crit_result_buffer_size(graph->edge_count, graph->node_count);
	h->tile_word_count = crit_reachability_tile_word_count(graph->node_count, nppc_tile_budget_bytes, device_properties.limits.maxStorageBufferRange);
	VkDeviceSize reachability_size = crit_reachability_tile_buffer_size(graph->node_count, h->tile_word_count);
	VkDeviceSize total_count_size = crit_total_count_buffer_size(graph->node_count);
	VkDeviceSize sizes[BINDING_COUNT] = {node_size, edge_size, node_size, edge_size, sizeof(uint32_t) * graph->node_count, value_size, value_size, value_size, value_size, animation_size, result_size, reachability_size, total_count_size, total_count_size};
	VkBufferUsageFlags usages[BINDING_COUNT];
	for (uint32_t i = 0; i < BINDING_COUNT; i++)
		usages[i] = i == BUF_TOTAL_COUNT_FWD || i == BUF_TOTAL_COUNT_REV ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	for (uint32_t i = 0; i < BINDING_COUNT; i++)
		if (create_buffer(h, i, sizes[i], usages[i]) != 0)
			return 1;
	CritEdge dummy_edge = {0};
	if (upload(h, BUF_OUT_NODES, graph->out_nodes, node_size) || upload(h, BUF_OUT_EDGES, graph->edge_count > 0 ? graph->out_edges : &dummy_edge, edge_size) || upload(h, BUF_IN_NODES, graph->in_nodes, node_size) || upload(h, BUF_IN_EDGES, graph->edge_count > 0 ? graph->in_edges : &dummy_edge, edge_size) || upload(h, BUF_LEVELS, graph->level_nodes, sizeof(uint32_t) * graph->node_count))
		return 1;
	VkDescriptorBufferInfo infos[BINDING_COUNT];
	VkWriteDescriptorSet writes[BINDING_COUNT];
	for (uint32_t i = 0; i < BINDING_COUNT; i++) {
		infos[i] = (VkDescriptorBufferInfo){h->buffers[i], 0, VK_WHOLE_SIZE};
		writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = h->descriptor_set, .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &infos[i]};
	}
	vkUpdateDescriptorSets(h->device, BINDING_COUNT, writes, 0, NULL);
	return 0;
}

static int harness_reset_run(Harness *h, const GraphSpec *graph)
{
	float *zeros = calloc(graph->node_count, sizeof(float));
	unsigned char *animation = calloc(1, sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * (graph->edge_count > 0 ? graph->edge_count : 1));
	unsigned char *result = calloc(1, crit_result_buffer_size(graph->edge_count, graph->node_count));
	if (!zeros || !animation || !result) {
		free(zeros);
		free(animation);
		free(result);
		return 1;
	}
	CritResultHeader *header = (CritResultHeader *)result;
	header->edge_count = graph->edge_count;
	header->node_count = graph->node_count;
	header->sink_node = UINT32_MAX;
	uint32_t *data = (uint32_t *)(result + sizeof(*header));
	for (uint32_t v = 0; v < graph->node_count; v++)
		data[crit_result_predecessor_offset(graph->edge_count) + v] = UINT32_MAX;
	size_t value_size = sizeof(float) * graph->node_count;
	int failed = upload(h, BUF_LNW, zeros, value_size) || upload(h, BUF_LNX, zeros, value_size) || upload(h, BUF_HEIGHT, zeros, value_size) || upload(h, BUF_DEPTH, zeros, value_size) || upload(h, BUF_EDGE_ANIM, animation, sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * (graph->edge_count > 0 ? graph->edge_count : 1)) || upload(h, BUF_RESULT, result, crit_result_buffer_size(graph->edge_count, graph->node_count));
	free(zeros);
	free(animation);
	free(result);
	return failed;
}

static void dispatch_tile(Harness *h, uint32_t offset, uint32_t count, uint32_t stage, uint32_t mode, uint32_t tile_word_offset, uint32_t tile_word_count)
{
	if (count == 0)
		return;
	CritPushConstants constants = {offset, count, stage, mode, tile_word_offset, tile_word_count};
	vkCmdPushConstants(h->command_buffer, h->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
	vkCmdDispatch(h->command_buffer, (count + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1, 1);
}

static void dispatch(Harness *h, uint32_t offset, uint32_t count, uint32_t stage, uint32_t mode)
{
	dispatch_tile(h, offset, count, stage, mode, 0, 0);
}

static void compute_barrier(Harness *h)
{
	VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(h->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

// Mirrors the tiles-outer/levels-inner batch accumulation renderer_criticality.c performs before
// its per-frame reveal loop: exact popcounts land in the total_count buffers, consumed by the
// STAGE_LNW/STAGE_LNX finalize path below exactly as in production.
static void run_nppc_batch_accumulation(Harness *h, const GraphSpec *graph)
{
	vkCmdFillBuffer(h->command_buffer, h->buffers[BUF_TOTAL_COUNT_FWD], 0, VK_WHOLE_SIZE, 0u);
	vkCmdFillBuffer(h->command_buffer, h->buffers[BUF_TOTAL_COUNT_REV], 0, VK_WHOLE_SIZE, 0u);
	VkMemoryBarrier fill_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
	vkCmdPipelineBarrier(h->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fill_barrier, 0, NULL, 0, NULL);

	uint32_t total_words = (uint32_t)crit_reachability_word_count(graph->node_count);
	uint32_t tile_words = h->tile_word_count;
	for (uint32_t tile_offset = 0; tile_offset < total_words; tile_offset += tile_words) {
		uint32_t tile_count = tile_offset + tile_words <= total_words ? tile_words : total_words - tile_offset;
		for (uint32_t level = 0; level < graph->num_levels; level++) {
			dispatch_tile(h, graph->level_offsets[level], graph->level_sizes[level], CRIT_STAGE_NPPC_ACCUMULATE_FWD, CRIT_WEIGHT_NPPC, tile_offset, tile_count);
			compute_barrier(h);
		}
		for (uint32_t i = 0; i < graph->num_levels; i++) {
			uint32_t level = graph->num_levels - 1 - i;
			dispatch_tile(h, graph->level_offsets[level], graph->level_sizes[level], CRIT_STAGE_NPPC_ACCUMULATE_REV, CRIT_WEIGHT_NPPC, tile_offset, tile_count);
			compute_barrier(h);
		}
	}
}

static int harness_run(Harness *h, const GraphSpec *graph, uint32_t mode)
{
	if (harness_reset_run(h, graph) != 0)
		return 1;
	VK_TRY(vkResetCommandBuffer(h->command_buffer, 0), "vkResetCommandBuffer");
	VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_TRY(vkBeginCommandBuffer(h->command_buffer, &begin), "vkBeginCommandBuffer");
	vkCmdBindPipeline(h->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, h->pipeline);
	vkCmdBindDescriptorSets(h->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, h->pipeline_layout, 0, 1, &h->descriptor_set, 0, NULL);
	if (mode == CRIT_WEIGHT_NPPC)
		run_nppc_batch_accumulation(h, graph);
	for (uint32_t level = 0; level < graph->num_levels; level++) {
		dispatch(h, graph->level_offsets[level], graph->level_sizes[level], CRIT_STAGE_LNW, mode);
		compute_barrier(h);
	}
	if (mode == CRIT_WEIGHT_SPC || mode == CRIT_WEIGHT_SPE || mode == CRIT_WEIGHT_NPPC)
		for (uint32_t i = 0; i < graph->num_levels; i++) {
			uint32_t level = graph->num_levels - 1 - i;
			dispatch(h, graph->level_offsets[level], graph->level_sizes[level], CRIT_STAGE_LNX, mode);
			compute_barrier(h);
		}
	dispatch(h, 0, graph->node_count, CRIT_STAGE_MATERIALIZE, mode);
	compute_barrier(h);
	for (uint32_t level = 0; level < graph->num_levels; level++) {
		dispatch(h, graph->level_offsets[level], graph->level_sizes[level], CRIT_STAGE_HEIGHT, mode);
		compute_barrier(h);
	}
	for (uint32_t i = 0; i < graph->num_levels; i++) {
		uint32_t level = graph->num_levels - 1 - i;
		dispatch(h, graph->level_offsets[level], graph->level_sizes[level], CRIT_STAGE_DEPTH, mode);
		compute_barrier(h);
	}
	dispatch(h, 0, graph->node_count, CRIT_STAGE_REDUCE, mode);
	compute_barrier(h);
	dispatch(h, 0, graph->node_count, CRIT_STAGE_FLAGS, mode);
	compute_barrier(h);
	dispatch(h, 0, 1, CRIT_STAGE_PATH_TRACE, mode);
	VkMemoryBarrier host_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
	vkCmdPipelineBarrier(h->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, NULL, 0, NULL);
	VK_TRY(vkEndCommandBuffer(h->command_buffer), "vkEndCommandBuffer");
	VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &h->command_buffer};
	VK_TRY(vkQueueSubmit(h->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
	VK_TRY(vkQueueWaitIdle(h->queue), "vkQueueWaitIdle");
	return 0;
}

static int check_float_array(const char *name, const float *got, const float *expected, uint32_t count)
{
	int failures = 0;
	for (uint32_t i = 0; i < count; i++)
		if (fabsf(got[i] - expected[i]) > TOLERANCE) {
			fprintf(stderr, "%s[%u]: got %.6f expected %.6f\n", name, i, (double)got[i], (double)expected[i]);
			failures++;
		}
	return failures;
}

static int check_uint_array(const char *name, const uint32_t *got, const uint32_t *expected, uint32_t count)
{
	int failures = 0;
	for (uint32_t i = 0; i < count; i++)
		if (got[i] != expected[i]) {
			fprintf(stderr, "%s[%u]: got %u expected %u\n", name, i, got[i], expected[i]);
			failures++;
		}
	return failures;
}

#define BASE_NODES 8
#define BASE_EDGES 10
#define BASE_LEVELS 5
#define LN2 0.69314718056f
#define LN5 1.60943791243f

static const CritNode base_out_nodes[BASE_NODES] = {{0, 3}, {3, 1}, {4, 1}, {5, 2}, {7, 1}, {8, 1}, {9, 0}, {9, 1}};
static const CritEdge base_out_edges[BASE_EDGES] = {{1, 0}, {2, 1}, {7, 2}, {3, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}, {6, 8}, {6, 9}};
static const CritNode base_in_nodes[BASE_NODES] = {{0, 0}, {0, 1}, {1, 1}, {2, 2}, {4, 1}, {5, 1}, {6, 3}, {9, 1}};
static const CritEdge base_in_edges[BASE_EDGES] = {{0, 0}, {0, 1}, {1, 3}, {2, 4}, {3, 5}, {3, 6}, {4, 7}, {5, 8}, {7, 9}, {0, 2}};
static const uint32_t base_level_nodes[BASE_NODES] = {0, 1, 2, 7, 3, 4, 5, 6};
static const uint32_t base_level_offsets[BASE_LEVELS] = {0, 1, 4, 5, 7};
static const uint32_t base_level_sizes[BASE_LEVELS] = {1, 3, 1, 2, 1};
static const GraphSpec base_graph = {BASE_NODES, BASE_EDGES, BASE_LEVELS, base_out_nodes, base_out_edges, base_in_nodes, base_in_edges, base_level_nodes, base_level_offsets, base_level_sizes};

#define PAPER_NODES 7
#define PAPER_EDGES 9
#define PAPER_LEVELS 6

static const CritNode paper_out_nodes[PAPER_NODES] = {{0, 2}, {2, 1}, {3, 2}, {5, 1}, {6, 2}, {8, 1}, {9, 0}};
static const CritEdge paper_out_edges[PAPER_EDGES] = {{1, 0}, {5, 1}, {2, 2}, {3, 3}, {4, 4}, {6, 5}, {5, 6}, {6, 7}, {6, 8}};
static const CritNode paper_in_nodes[PAPER_NODES] = {{0, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 2}, {6, 3}};
static const CritEdge paper_in_edges[PAPER_EDGES] = {{0, 0}, {1, 2}, {2, 3}, {2, 4}, {0, 1}, {4, 6}, {3, 5}, {4, 7}, {5, 8}};
static const uint32_t paper_level_nodes[PAPER_NODES] = {0, 1, 2, 3, 4, 5, 6};
static const uint32_t paper_level_offsets[PAPER_LEVELS] = {0, 1, 2, 3, 5, 6};
static const uint32_t paper_level_sizes[PAPER_LEVELS] = {1, 1, 1, 2, 1, 1};
static const GraphSpec paper_graph = {PAPER_NODES, PAPER_EDGES, PAPER_LEVELS, paper_out_nodes, paper_out_edges, paper_in_nodes, paper_in_edges, paper_level_nodes, paper_level_offsets, paper_level_sizes};

static int check_base_mode(Harness *h, uint32_t mode, const char *name)
{
	if (harness_run(h, &base_graph, mode) != 0)
		return 1;
	float lnw[BASE_NODES];
	float lnx[BASE_NODES];
	float height[BASE_NODES];
	float depth[BASE_NODES];
	unsigned char result_bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * (BASE_EDGES + 3 * BASE_NODES)];
	unsigned char animation_bytes[sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * BASE_EDGES];
	if (download(h, BUF_LNW, lnw, sizeof(lnw)) || download(h, BUF_LNX, lnx, sizeof(lnx)) || download(h, BUF_HEIGHT, height, sizeof(height)) || download(h, BUF_DEPTH, depth, sizeof(depth)) || download(h, BUF_RESULT, result_bytes, sizeof(result_bytes)) || download(h, BUF_EDGE_ANIM, animation_bytes, sizeof(animation_bytes)))
		return 1;
	static const float regular_lnw[BASE_NODES] = {0, 0, 0, LN2, LN2, LN2, LN5, 0};
	static const float splc_lnw[BASE_NODES] = {0, LN2, LN2, LN5, 1.79175946923f, 1.79175946923f, 2.70805020110f, LN2};
	static const float suffix[BASE_NODES] = {LN5, LN2, LN2, LN2, 0, 0, 0, 0};
	static const float zero[BASE_NODES] = {0};
	static const float unit_height[BASE_NODES] = {0, 1, 1, 2, 3, 3, 4, 1};
	static const float unit_depth[BASE_NODES] = {4, 3, 3, 2, 1, 1, 0, 1};
	static const float splc_height[BASE_NODES] = {0, 1, 1, 3, 8, 8, 14, 1};
	static const float splc_depth[BASE_NODES] = {14, 13, 13, 11, 6, 6, 0, 2};
	static const float spc_height[BASE_NODES] = {0, 2, 2, 4, 6, 6, 8, 1};
	static const float spc_depth[BASE_NODES] = {8, 6, 6, 4, 2, 2, 0, 1};
	static const float spe_height[BASE_NODES] = {0, LN2, LN2, 2 * LN2, 3 * LN2, 3 * LN2, 4 * LN2, 0};
	static const float spe_depth[BASE_NODES] = {4 * LN2, 3 * LN2, 3 * LN2, 2 * LN2, LN2, LN2, 0, 0};
	const float *expected_height = mode == CRIT_WEIGHT_UNIT ? unit_height : (mode == CRIT_WEIGHT_SPLC ? splc_height : (mode == CRIT_WEIGHT_SPC ? spc_height : spe_height));
	const float *expected_depth = mode == CRIT_WEIGHT_UNIT ? unit_depth : (mode == CRIT_WEIGHT_SPLC ? splc_depth : (mode == CRIT_WEIGHT_SPC ? spc_depth : spe_depth));
	float maximum = mode == CRIT_WEIGHT_UNIT ? 4.0f : (mode == CRIT_WEIGHT_SPLC ? 14.0f : (mode == CRIT_WEIGHT_SPC ? 8.0f : 4 * LN2));
	CritResultHeader *header = (CritResultHeader *)result_bytes;
	uint32_t *data = (uint32_t *)(result_bytes + sizeof(*header));
	EdgeAnimHeader *animation_header = (EdgeAnimHeader *)animation_bytes;
	EdgeAnim *animation_edges = (EdgeAnim *)(animation_bytes + sizeof(*animation_header));
	float expected_weights[BASE_EDGES];
	float expected_strengths[BASE_EDGES];
	float got_weights[BASE_EDGES];
	float got_strengths[BASE_EDGES];
	float expected_strength_max = 0.0f;
	for (uint32_t e = 0; e < BASE_EDGES; e++) {
		uint32_t source = e < 3 ? 0 : (e < 4 ? 1 : (e < 5 ? 2 : (e < 7 ? 3 : (e < 8 ? 4 : (e < 9 ? 5 : 7)))));
		uint32_t target = base_out_edges[e].node;
		float log_weight = mode == CRIT_WEIGHT_SPLC ? splc_lnw[source] : regular_lnw[source] + ((mode == CRIT_WEIGHT_SPC || mode == CRIT_WEIGHT_SPE) ? suffix[target] : 0.0f);
		expected_weights[e] = mode == CRIT_WEIGHT_UNIT ? 1.0f : (mode == CRIT_WEIGHT_SPE ? log_weight : expf(log_weight));
		expected_strengths[e] = mode == CRIT_WEIGHT_UNIT ? 1.0f : (mode == CRIT_WEIGHT_SPE ? fmaxf(1.0f + log_weight, 0.0f) : (log_weight > 20.0f ? log_weight : logf(1.0f + expf(log_weight))));
		got_weights[e] = bits_float(data[crit_result_weight_offset() + e]);
		got_strengths[e] = animation_edges[e].strength;
		expected_strength_max = fmaxf(expected_strength_max, expected_strengths[e]);
	}
	static const uint32_t predecessors[BASE_NODES] = {UINT32_MAX, 0, 0, 1, 3, 3, 4, 0};
	static const uint32_t basket[BASE_NODES] = {1, 1, 1, 1, 1, 1, 1, 0};
	static const uint32_t path[BASE_NODES] = {1, 1, 0, 1, 1, 0, 1, 0};
	int failures = 0;
	failures += check_float_array("lnW", lnw, mode == CRIT_WEIGHT_SPLC ? splc_lnw : regular_lnw, BASE_NODES);
	failures += check_float_array("lnX", lnx, mode == CRIT_WEIGHT_SPC || mode == CRIT_WEIGHT_SPE ? suffix : zero, BASE_NODES);
	failures += check_float_array("analysis", got_weights, expected_weights, BASE_EDGES);
	failures += check_float_array("presentation", got_strengths, expected_strengths, BASE_EDGES);
	failures += check_float_array("height", height, expected_height, BASE_NODES);
	failures += check_float_array("depth", depth, expected_depth, BASE_NODES);
	failures += check_uint_array("predecessor", data + crit_result_predecessor_offset(BASE_EDGES), predecessors, BASE_NODES);
	failures += check_uint_array("basket", data + crit_result_basket_offset(BASE_EDGES, BASE_NODES), basket, BASE_NODES);
	failures += check_uint_array("path", data + crit_result_path_offset(BASE_EDGES, BASE_NODES), path, BASE_NODES);
	if (header->status != 0 || fabsf(bits_float(header->criticality_max_bits) - maximum) > TOLERANCE || fabsf(bits_float(header->sink_height_bits) - maximum) > TOLERANCE || header->sink_node != 6 || fabsf(bits_float(animation_header->strength_max_bits) - expected_strength_max) > TOLERANCE) {
		fprintf(stderr, "%s header mismatch\n", name);
		failures++;
	}
	printf("%s complete pipeline: %s\n", name, failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_nppc_paper_oracle(Harness *h)
{
	if (harness_upload_graph(h, &paper_graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &paper_graph, CRIT_WEIGHT_NPPC))
		return 1;
	float lnw[PAPER_NODES];
	float lnx[PAPER_NODES];
	float height[PAPER_NODES];
	float depth[PAPER_NODES];
	unsigned char result_bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * (PAPER_EDGES + 3 * PAPER_NODES)];
	if (download(h, BUF_LNW, lnw, sizeof(lnw)) || download(h, BUF_LNX, lnx, sizeof(lnx)) || download(h, BUF_HEIGHT, height, sizeof(height)) || download(h, BUF_DEPTH, depth, sizeof(depth)) || download(h, BUF_RESULT, result_bytes, sizeof(result_bytes)))
		return 1;

	static const float expected_lnw[PAPER_NODES] = {0.0f, 0.69314718056f, 1.09861228867f, 1.38629436112f, 1.38629436112f, 1.60943791243f, 1.94591014906f};
	static const float expected_lnx[PAPER_NODES] = {1.94591014906f, 1.79175946923f, 1.60943791243f, 0.69314718056f, 1.09861228867f, 0.69314718056f, 0.0f};
	static const float expected_height[PAPER_NODES] = {0.0f, 6.0f, 16.0f, 22.0f, 25.0f, 33.0f, 38.0f};
	static const float expected_depth[PAPER_NODES] = {38.0f, 32.0f, 22.0f, 4.0f, 13.0f, 5.0f, 0.0f};
	static const float expected_weights[PAPER_EDGES] = {6.0f, 2.0f, 10.0f, 6.0f, 9.0f, 4.0f, 8.0f, 4.0f, 5.0f};
	static const char *arc_names[PAPER_EDGES] = {"3->5", "3->21", "5->12", "12->15", "12->20", "15->22", "20->21", "20->22", "21->22"};
	static const uint32_t expected_predecessors[PAPER_NODES] = {UINT32_MAX, 0, 1, 2, 2, 4, 5};
	static const uint32_t expected_basket[PAPER_NODES] = {1, 1, 1, 0, 1, 1, 1};
	static const uint32_t expected_path[PAPER_NODES] = {1, 1, 1, 0, 1, 1, 1};

	CritResultHeader *header = (CritResultHeader *)result_bytes;
	uint32_t *data = (uint32_t *)(result_bytes + sizeof(*header));
	int failures = 0;
	failures += check_float_array("NPPC lnW", lnw, expected_lnw, PAPER_NODES);
	failures += check_float_array("NPPC lnX", lnx, expected_lnx, PAPER_NODES);
	failures += check_float_array("NPPC height", height, expected_height, PAPER_NODES);
	failures += check_float_array("NPPC depth", depth, expected_depth, PAPER_NODES);
	for (uint32_t e = 0; e < PAPER_EDGES; e++) {
		float weight = bits_float(data[crit_result_weight_offset() + e]);
		if (fabsf(weight - expected_weights[e]) > TOLERANCE) {
			fprintf(stderr, "NPPC %s: got %.6f expected %.6f\n", arc_names[e], (double)weight, (double)expected_weights[e]);
			failures++;
		}
	}
	failures += check_uint_array("NPPC predecessor", data + crit_result_predecessor_offset(PAPER_EDGES), expected_predecessors, PAPER_NODES);
	failures += check_uint_array("NPPC basket", data + crit_result_basket_offset(PAPER_EDGES, PAPER_NODES), expected_basket, PAPER_NODES);
	failures += check_uint_array("NPPC path", data + crit_result_path_offset(PAPER_EDGES, PAPER_NODES), expected_path, PAPER_NODES);
	if (header->status != 0 || fabsf(bits_float(header->criticality_max_bits) - 38.0f) > TOLERANCE || fabsf(bits_float(header->sink_height_bits) - 38.0f) > TOLERANCE || header->sink_node != 6) {
		fprintf(stderr, "NPPC paper oracle header mismatch\n");
		failures++;
	}
	printf("NPPC 1989 paper oracle: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_reachability_sizing(void)
{
	int failures = 0;
	size_t generous_budget = (size_t)1024 * 1024 * 1024;
	if (crit_reachability_word_count(7) != 1)
		failures++;
	if (crit_reachability_tile_word_count(7, generous_budget, generous_budget) != 1)
		failures++;
	if (crit_reachability_tile_word_count(33, generous_budget, generous_budget) != 2)
		failures++;
	// A budget that only fits one word/node (4*33 bytes) must clamp the tile width to 1, not 2.
	if (crit_reachability_tile_word_count(33, sizeof(uint32_t) * 33, generous_budget) != 1)
		failures++;
	// The device's maxStorageBufferRange must clamp just as the budget does.
	if (crit_reachability_tile_word_count(33, generous_budget, sizeof(uint32_t) * 33) != 1)
		failures++;
	if (crit_reachability_tile_buffer_size(33, 1) != 132 || crit_reachability_tile_buffer_size(33, 2) != 264)
		failures++;
	if (crit_total_count_buffer_size(33) != 132)
		failures++;
	printf("NPPC reachability tile sizing: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_nppc_chain_parallel(Harness *h)
{
	static const CritNode out_nodes[4] = {{0, 2}, {2, 1}, {3, 1}, {4, 0}};
	static const CritEdge out_edges[4] = {{1, 0}, {1, 1}, {2, 2}, {3, 3}};
	static const CritNode in_nodes[4] = {{0, 0}, {0, 2}, {2, 1}, {3, 1}};
	static const CritEdge in_edges[4] = {{0, 0}, {0, 1}, {1, 2}, {2, 3}};
	static const uint32_t levels[4] = {0, 1, 2, 3};
	static const uint32_t offsets[4] = {0, 1, 2, 3};
	static const uint32_t sizes[4] = {1, 1, 1, 1};
	GraphSpec graph = {4, 4, 4, out_nodes, out_edges, in_nodes, in_edges, levels, offsets, sizes};
	if (harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, CRIT_WEIGHT_NPPC))
		return 1;
	unsigned char bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * 16];
	if (download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return 1;
	uint32_t *data = (uint32_t *)(bytes + sizeof(CritResultHeader));
	static const float expected[4] = {3.0f, 3.0f, 4.0f, 3.0f};
	int failures = 0;
	for (uint32_t e = 0; e < 4; e++) {
		float weight = bits_float(data[crit_result_weight_offset() + e]);
		if (fabsf(weight - expected[e]) > TOLERANCE)
			failures++;
	}
	printf("NPPC chain and parallel edges: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

// A 40-node chain forces crit_reachability_word_count() to 2 words, and forcing the tile budget
// down to one word/node makes crit_reachability_tile_word_count() return 1 — genuinely exercising
// the tiles-outer/levels-inner multi-pass accumulation (not just the single-tile collapse every
// other NPPC test above exercises, since all of them have <=32 nodes). For a pure chain 0->1->...->N-1,
// |R^{inv*}(k)| = k+1 and |R^*(k+1)| = N-1-k, so edge (k, k+1)'s exact NPPC weight is a closed form:
// (k+1)*(N-1-k), independently checkable without a bitset.
static int check_nppc_multi_tile(Harness *h)
{
	const uint32_t node_count = 40;
	const uint32_t edge_count = node_count - 1;
	CritNode *out_nodes = calloc(node_count, sizeof(*out_nodes));
	CritNode *in_nodes = calloc(node_count, sizeof(*in_nodes));
	CritEdge *out_edges = calloc(edge_count, sizeof(*out_edges));
	CritEdge *in_edges = calloc(edge_count, sizeof(*in_edges));
	uint32_t *levels = malloc(sizeof(*levels) * node_count);
	uint32_t *offsets = malloc(sizeof(*offsets) * node_count);
	uint32_t *sizes = malloc(sizeof(*sizes) * node_count);
	if (!out_nodes || !in_nodes || !out_edges || !in_edges || !levels || !offsets || !sizes) {
		free(out_nodes);
		free(in_nodes);
		free(out_edges);
		free(in_edges);
		free(levels);
		free(offsets);
		free(sizes);
		return 1;
	}
	for (uint32_t v = 0; v < node_count; v++) {
		levels[v] = v;
		offsets[v] = v;
		sizes[v] = 1;
		out_nodes[v] = (CritNode){v, v + 1 < node_count ? 1 : 0};
		in_nodes[v] = (CritNode){v == 0 ? 0 : v - 1, v == 0 ? 0 : 1};
		if (v + 1 < node_count) {
			out_edges[v] = (CritEdge){v + 1, v};
			in_edges[v] = (CritEdge){v, v};
		}
	}
	GraphSpec graph = {node_count, edge_count, node_count, out_nodes, out_edges, in_nodes, in_edges, levels, offsets, sizes};
	int failures = harness_upload_graph(h, &graph, sizeof(uint32_t)) || harness_run(h, &graph, CRIT_WEIGHT_NPPC);
	if (!failures && h->tile_word_count != 1) {
		fprintf(stderr, "multi-tile NPPC: expected tile_word_count=1, got %u\n", h->tile_word_count);
		failures++;
	}
	unsigned char *result = malloc(crit_result_buffer_size(edge_count, node_count));
	if (!failures && result && !download(h, BUF_RESULT, result, crit_result_buffer_size(edge_count, node_count))) {
		uint32_t *data = (uint32_t *)(result + sizeof(CritResultHeader));
		for (uint32_t e = 0; e < edge_count; e++) {
			float expected = (float)(e + 1) * (float)(node_count - 1 - e);
			float weight = bits_float(data[crit_result_weight_offset() + e]);
			// Weights here (up to ~400) are far larger than the other NPPC fixtures (<=10), so the
			// same relative float32 log/exp round-trip error needs a scale-aware tolerance, not the
			// fixed absolute TOLERANCE used for small-magnitude checks elsewhere in this file.
			float scale_tolerance = fmaxf(TOLERANCE, expected * 1e-5f);
			if (fabsf(weight - expected) > scale_tolerance) {
				fprintf(stderr, "multi-tile NPPC edge %u: got %.6f expected %.6f\n", e, (double)weight, (double)expected);
				failures++;
			}
		}
	} else if (!failures) {
		failures++;
	}
	free(result);
	free(out_nodes);
	free(in_nodes);
	free(out_edges);
	free(in_edges);
	free(levels);
	free(offsets);
	free(sizes);
	printf("NPPC forced multi-tile chain: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_zero_weight_tie(Harness *h)
{
	static const CritNode out_nodes[3] = {{0, 1}, {1, 1}, {2, 0}};
	static const CritEdge out_edges[2] = {{2, 1}, {2, 0}};
	static const CritNode in_nodes[3] = {{0, 0}, {0, 0}, {0, 2}};
	static const CritEdge in_edges[2] = {{1, 0}, {0, 1}};
	static const uint32_t levels[3] = {0, 1, 2};
	static const uint32_t offsets[2] = {0, 2};
	static const uint32_t sizes[2] = {2, 1};
	GraphSpec graph = {3, 2, 2, out_nodes, out_edges, in_nodes, in_edges, levels, offsets, sizes};
	if (harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, CRIT_WEIGHT_SPE))
		return 1;
	unsigned char bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * 11];
	if (download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return 1;
	CritResultHeader *header = (CritResultHeader *)bytes;
	uint32_t *data = (uint32_t *)(bytes + sizeof(*header));
	uint32_t expected_predecessors[3] = {UINT32_MAX, UINT32_MAX, 1};
	uint32_t expected_path[3] = {0, 1, 1};
	int failures = check_uint_array("zero predecessor", data + crit_result_predecessor_offset(2), expected_predecessors, 3) + check_uint_array("zero path", data + crit_result_path_offset(2, 3), expected_path, 3);
	if (header->status != 0 || header->sink_node != 2)
		failures++;
	printf("zero-weight SPE tie: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_empty_edges(Harness *h)
{
	static const CritNode nodes[2] = {{0, 0}, {0, 0}};
	static const uint32_t levels[2] = {0, 1};
	static const uint32_t offsets[1] = {0};
	static const uint32_t sizes[1] = {2};
	GraphSpec graph = {2, 0, 1, nodes, NULL, nodes, NULL, levels, offsets, sizes};
	if (harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, CRIT_WEIGHT_UNIT))
		return 1;
	unsigned char bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * 6];
	if (download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return 1;
	CritResultHeader *header = (CritResultHeader *)bytes;
	uint32_t *data = (uint32_t *)(bytes + sizeof(*header));
	uint32_t expected_basket[2] = {1, 1};
	uint32_t expected_path[2] = {1, 0};
	int failures = check_uint_array("empty basket", data + crit_result_basket_offset(0, 2), expected_basket, 2) + check_uint_array("empty path", data + crit_result_path_offset(0, 2), expected_path, 2);
	if (header->status != 0 || header->sink_node != 0)
		failures++;
	printf("empty-edge sink tie: %s\n", failures == 0 ? "ok" : "failed");
	if (harness_run(h, &graph, CRIT_WEIGHT_NPPC) || download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return failures + 1;
	header = (CritResultHeader *)bytes;
	data = (uint32_t *)(bytes + sizeof(*header));
	failures += check_uint_array("empty NPPC basket", data + crit_result_basket_offset(0, 2), expected_basket, 2);
	failures += check_uint_array("empty NPPC path", data + crit_result_path_offset(0, 2), expected_path, 2);
	if (header->status != 0 || header->sink_node != 0)
		failures++;
	printf("empty-edge NPPC tie: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_overflow(Harness *h, uint32_t mode, const char *name)
{
	const uint32_t node_count = 140;
	const uint32_t edge_count = 2 * (node_count - 1);
	CritNode *out_nodes = calloc(node_count, sizeof(*out_nodes));
	CritNode *in_nodes = calloc(node_count, sizeof(*in_nodes));
	CritEdge *out_edges = calloc(edge_count, sizeof(*out_edges));
	CritEdge *in_edges = calloc(edge_count, sizeof(*in_edges));
	uint32_t *levels = malloc(sizeof(*levels) * node_count);
	uint32_t *offsets = malloc(sizeof(*offsets) * node_count);
	uint32_t *sizes = malloc(sizeof(*sizes) * node_count);
	if (!out_nodes || !in_nodes || !out_edges || !in_edges || !levels || !offsets || !sizes)
		return 1;
	for (uint32_t v = 0; v < node_count; v++) {
		levels[v] = v;
		offsets[v] = v;
		sizes[v] = 1;
		out_nodes[v] = (CritNode){2 * v, v + 1 < node_count ? 2 : 0};
		in_nodes[v] = (CritNode){v == 0 ? 0 : 2 * (v - 1), v == 0 ? 0 : 2};
		if (v + 1 < node_count)
			for (uint32_t i = 0; i < 2; i++) {
				uint32_t edge = 2 * v + i;
				out_edges[edge] = (CritEdge){v + 1, edge};
				in_edges[edge] = (CritEdge){v, edge};
			}
	}
	GraphSpec graph = {node_count, edge_count, node_count, out_nodes, out_edges, in_nodes, in_edges, levels, offsets, sizes};
	int failures = harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, mode);
	unsigned char *result = malloc(crit_result_buffer_size(edge_count, node_count));
	unsigned char *animation = malloc(sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * edge_count);
	if (!failures && result && animation && !download(h, BUF_RESULT, result, crit_result_buffer_size(edge_count, node_count)) && !download(h, BUF_EDGE_ANIM, animation, sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * edge_count)) {
		CritResultHeader *header = (CritResultHeader *)result;
		EdgeAnim *edges = (EdgeAnim *)(animation + sizeof(EdgeAnimHeader));
		if ((header->status & CRIT_RESULT_OVERFLOW) == 0 || (header->status & CRIT_RESULT_INVALID) != 0)
			failures++;
		for (uint32_t e = 0; e < edge_count; e++)
			if (!isfinite(edges[e].strength)) {
				failures++;
				break;
			}
	} else if (!failures) {
		failures++;
	}
	free(result);
	free(animation);
	free(out_nodes);
	free(in_nodes);
	free(out_edges);
	free(in_edges);
	free(levels);
	free(offsets);
	free(sizes);
	printf("%s overflow: %s\n", name, failures == 0 ? "ok" : "failed");
	return failures;
}

int main(void)
{
	Harness harness;
	if (harness_init(&harness) != 0) {
		harness_destroy(&harness);
		return 1;
	}
	if (harness_upload_graph(&harness, &base_graph, CRIT_NPPC_TILE_BUDGET_BYTES) != 0) {
		harness_destroy(&harness);
		return 1;
	}
	int failures = 0;
	failures += check_base_mode(&harness, CRIT_WEIGHT_SPLC, "SPLC");
	failures += check_base_mode(&harness, CRIT_WEIGHT_UNIT, "Unit");
	failures += check_base_mode(&harness, CRIT_WEIGHT_SPC, "SPC");
	failures += check_base_mode(&harness, CRIT_WEIGHT_SPE, "SPE");
	failures += check_nppc_paper_oracle(&harness);
	failures += check_reachability_sizing();
	failures += check_nppc_chain_parallel(&harness);
	failures += check_nppc_multi_tile(&harness);
	failures += check_zero_weight_tie(&harness);
	failures += check_empty_edges(&harness);
	failures += check_overflow(&harness, CRIT_WEIGHT_SPLC, "SPLC");
	failures += check_overflow(&harness, CRIT_WEIGHT_SPC, "SPC");
	harness_destroy(&harness);
	if (failures != 0) {
		fprintf(stderr, "criticality_test: %d failures\n", failures);
		return 1;
	}
	printf("criticality_test: all checks passed\n");
	return 0;
}
