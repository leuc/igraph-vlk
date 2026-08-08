/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "criticality_test_harness.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORKGROUP_SIZE 64

#define VK_TRY(expression, message) \
	do { \
		VkResult result = (expression); \
		if (result != VK_SUCCESS) { \
			fprintf(stderr, "%s failed (VkResult %d)\n", message, result); \
			return 1; \
		} \
	} while (0)

float bits_float(uint32_t bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

uint32_t *result_data(unsigned char *bytes)
{
	return (uint32_t *)(bytes + sizeof(CritResultHeader));
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

int download(Harness *h, uint32_t binding, void *data, size_t size)
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
	VkResult shader_result = vkCreateShaderModule(h->device, &info, NULL, shader);
	free(code);
	VK_TRY(shader_result, "vkCreateShaderModule");
	return 0;
}

int harness_init(Harness *h)
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

void harness_destroy(Harness *h)
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

int harness_upload_graph(Harness *h, const GraphSpec *graph, size_t nppc_tile_budget_bytes)
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
	uint32_t *data = result_data(result);
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

int harness_run(Harness *h, const GraphSpec *graph, uint32_t mode)
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
	dispatch(h, 0, 1, CRIT_STAGE_GLOBAL_TRACE, mode);
	VkMemoryBarrier host_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
	vkCmdPipelineBarrier(h->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, NULL, 0, NULL);
	VK_TRY(vkEndCommandBuffer(h->command_buffer), "vkEndCommandBuffer");
	VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &h->command_buffer};
	VK_TRY(vkQueueSubmit(h->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
	VK_TRY(vkQueueWaitIdle(h->queue), "vkQueueWaitIdle");
	return 0;
}

int check_float_array(const char *name, const float *got, const float *expected, uint32_t count)
{
	int failures = 0;
	for (uint32_t i = 0; i < count; i++)
		if (fabsf(got[i] - expected[i]) > TOLERANCE) {
			fprintf(stderr, "%s[%u]: got %.6f expected %.6f\n", name, i, (double)got[i], (double)expected[i]);
			failures++;
		}
	return failures;
}

int check_uint_array(const char *name, const uint32_t *got, const uint32_t *expected, uint32_t count)
{
	int failures = 0;
	for (uint32_t i = 0; i < count; i++)
		if (got[i] != expected[i]) {
			fprintf(stderr, "%s[%u]: got %u expected %u\n", name, i, got[i], expected[i]);
			failures++;
		}
	return failures;
}

bool build_chain_graph(ChainGraph *cg, uint32_t node_count, uint32_t edges_per_step)
{
	uint32_t edge_count = edges_per_step * (node_count - 1);
	memset(cg, 0, sizeof(*cg));
	cg->out_nodes = calloc(node_count, sizeof(*cg->out_nodes));
	cg->in_nodes = calloc(node_count, sizeof(*cg->in_nodes));
	cg->out_edges = calloc(edge_count > 0 ? edge_count : 1, sizeof(*cg->out_edges));
	cg->in_edges = calloc(edge_count > 0 ? edge_count : 1, sizeof(*cg->in_edges));
	cg->levels = malloc(sizeof(*cg->levels) * node_count);
	cg->offsets = malloc(sizeof(*cg->offsets) * node_count);
	cg->sizes = malloc(sizeof(*cg->sizes) * node_count);
	if (!cg->out_nodes || !cg->in_nodes || !cg->out_edges || !cg->in_edges || !cg->levels || !cg->offsets || !cg->sizes)
		return false;
	for (uint32_t v = 0; v < node_count; v++) {
		cg->levels[v] = v;
		cg->offsets[v] = v;
		cg->sizes[v] = 1;
		cg->out_nodes[v] = (CritNode){edges_per_step * v, v + 1 < node_count ? edges_per_step : 0};
		cg->in_nodes[v] = (CritNode){v == 0 ? 0 : edges_per_step * (v - 1), v == 0 ? 0 : edges_per_step};
		if (v + 1 < node_count)
			for (uint32_t i = 0; i < edges_per_step; i++) {
				uint32_t edge = edges_per_step * v + i;
				cg->out_edges[edge] = (CritEdge){v + 1, edge};
				cg->in_edges[edge] = (CritEdge){v, edge};
			}
	}
	return true;
}

void free_chain_graph(ChainGraph *cg)
{
	free(cg->out_nodes);
	free(cg->in_nodes);
	free(cg->out_edges);
	free(cg->in_edges);
	free(cg->levels);
	free(cg->offsets);
	free(cg->sizes);
}
