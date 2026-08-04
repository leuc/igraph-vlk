/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless Vulkan test for shaders/criticality.comp — the four generalised
 * height/depth sweeps of Price & Evans, "Understanding Main Path Analysis",
 * arXiv:2512.12355 section 2.5.
 *
 * No window or surface is needed: the shader is a pure compute workload, so
 * the test drives it against a hand-checked DAG and compares every per-node
 * result to values worked out on paper.
 *
 *      S ---> A ---> C ---> D ---> T          (nodes 0,1,3,4,6)
 *       \     ^      \             ^
 *        \    |       +--> E ------+          (node 5)
 *         \-> B (node 2, also into C)
 *          \
 *           \-> F ------------------+         (node 7, the shortcut)
 *
 * The S -> F -> T shortcut is the only node off every optimal path, so it is
 * the only node with nonzero criticality under either weighting.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define NODE_COUNT 8
#define OUT_EDGE_COUNT 10
#define IN_EDGE_COUNT 10
#define NUM_LEVELS 5
#define BINDING_COUNT 11
#define WORKGROUP_SIZE 64

#define CRIT_STAGE_LNW 0u
#define CRIT_STAGE_LNX 1u
#define CRIT_STAGE_HEIGHT 2u
#define CRIT_STAGE_DEPTH 3u
#define CRIT_WEIGHT_UNIT 0u
#define CRIT_WEIGHT_SPE 1u
#define CRIT_WEIGHT_SPC 2u

#define TOLERANCE 1e-4f

typedef struct
{
	uint32_t edge_offset;
	uint32_t degree;
} CritNode;

typedef struct
{
	uint32_t node;
	uint32_t pad;
} CritEdge;

typedef struct
{
	uint32_t target_node;
	float weight;
} DisplayEdge;

typedef struct
{
	uint32_t level_offset;
	uint32_t num_nodes_in_level;
	uint32_t stage;
	uint32_t weight_mode;
} CritPushConstants;

// ---------------------------------------------------------------------------
// The test DAG
// ---------------------------------------------------------------------------

// S=0 A=1 B=2 C=3 D=4 E=5 T=6 F=7
// Edges: S->A S->B S->F  A->C B->C  C->D C->E  D->T E->T  F->T
static const CritNode g_out_nodes[NODE_COUNT] = {
	{0, 3}, // S -> A, B, F
	{3, 1}, // A -> C
	{4, 1}, // B -> C
	{5, 2}, // C -> D, E
	{7, 1}, // D -> T
	{8, 1}, // E -> T
	{9, 0}, // T (sink)
	{9, 1}, // F -> T
};

static const uint32_t g_out_targets[OUT_EDGE_COUNT] = {1, 2, 7, 3, 3, 4, 5, 6, 6, 6};
static const uint32_t g_out_sources[OUT_EDGE_COUNT] = {0, 0, 0, 1, 2, 3, 3, 4, 5, 7};

static const CritNode g_in_nodes[NODE_COUNT] = {
	{0, 0}, // S (source)
	{0, 1}, // A <- S
	{1, 1}, // B <- S
	{2, 2}, // C <- A, B
	{4, 1}, // D <- C
	{5, 1}, // E <- C
	{6, 3}, // T <- D, E, F
	{9, 1}, // F <- S
};

static const uint32_t g_in_sources[IN_EDGE_COUNT] = {0, 0, 1, 2, 3, 3, 4, 5, 7, 0};

// Nodes grouped by level: {S} {A,B,F} {C} {D,E} {T}
static const uint32_t g_level_perm[NODE_COUNT] = {0, 1, 2, 7, 3, 4, 5, 6};
static const uint32_t g_level_offsets[NUM_LEVELS] = {0, 1, 4, 5, 7};
static const uint32_t g_level_sizes[NUM_LEVELS] = {1, 3, 1, 2, 1};

// Path counts: W = 1,1,1,2,2,2,5,1   X = 5,2,2,2,1,1,1,1
#define LN2 0.69314718f
#define LN5 1.60943791f
static const float g_expect_lnw[NODE_COUNT] = {0.0f, 0.0f, 0.0f, LN2, LN2, LN2, LN5, 0.0f};
static const float g_expect_lnx[NODE_COUNT] = {LN5, LN2, LN2, LN2, 0.0f, 0.0f, 0.0f, 0.0f};

static const float g_expect_unit_height[NODE_COUNT] = {0, 1, 1, 2, 3, 3, 4, 1};
static const float g_expect_unit_depth[NODE_COUNT] = {4, 3, 3, 2, 1, 1, 0, 1};
static const float g_expect_unit_criticality[NODE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 2};

static const float g_expect_spe_height[NODE_COUNT] = {0.0f, LN2, LN2, 2 * LN2, 3 * LN2, 3 * LN2, 4 * LN2, 0.0f};
static const float g_expect_spe_depth[NODE_COUNT] = {4 * LN2, 3 * LN2, 3 * LN2, 2 * LN2, LN2, LN2, 0.0f, 0.0f};
static const float g_expect_spe_criticality[NODE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 4 * LN2};

// ---------------------------------------------------------------------------
// Minimal headless Vulkan compute harness
// ---------------------------------------------------------------------------

typedef struct
{
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t queue_family;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool desc_pool;
	VkDescriptorSet desc_set;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkCommandPool cmd_pool;
	VkCommandBuffer cmd_buf;
	VkBuffer buffers[BINDING_COUNT];
	VkDeviceMemory memories[BINDING_COUNT];
} Harness;

#define VK_TRY(expr, msg) \
	do { \
		VkResult _r = (expr); \
		if (_r != VK_SUCCESS) { \
			fprintf(stderr, "%s failed (VkResult %d)\n", msg, _r); \
			return 1; \
		} \
	} while (0)

static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_filter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties props;
	vkGetPhysicalDeviceMemoryProperties(physical, &props);
	for (uint32_t i = 0; i < props.memoryTypeCount; i++)
		if ((type_filter & (1u << i)) && (props.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	return UINT32_MAX;
}

static int create_buffer(Harness *h, int index, VkDeviceSize size)
{
	VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_TRY(vkCreateBuffer(h->device, &info, NULL, &h->buffers[index]), "vkCreateBuffer");

	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(h->device, h->buffers[index], &reqs);
	uint32_t type = find_memory_type(h->physical, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (type == UINT32_MAX) {
		fprintf(stderr, "no host-visible coherent memory type\n");
		return 1;
	}

	VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = reqs.size, .memoryTypeIndex = type};
	VK_TRY(vkAllocateMemory(h->device, &alloc, NULL, &h->memories[index]), "vkAllocateMemory");
	VK_TRY(vkBindBufferMemory(h->device, h->buffers[index], h->memories[index], 0), "vkBindBufferMemory");
	return 0;
}

static int upload(Harness *h, int index, const void *data, size_t size)
{
	void *mapped = NULL;
	VK_TRY(vkMapMemory(h->device, h->memories[index], 0, size, 0, &mapped), "vkMapMemory (upload)");
	memcpy(mapped, data, size);
	vkUnmapMemory(h->device, h->memories[index]);
	return 0;
}

static int download(Harness *h, int index, void *out, size_t size)
{
	void *mapped = NULL;
	VK_TRY(vkMapMemory(h->device, h->memories[index], 0, size, 0, &mapped), "vkMapMemory (download)");
	memcpy(out, mapped, size);
	vkUnmapMemory(h->device, h->memories[index]);
	return 0;
}

static int load_shader(Harness *h, const char *path, VkShaderModule *out)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot open shader %s\n", path);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint32_t *code = malloc((size_t)size);
	if (!code || fread(code, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "cannot read shader %s\n", path);
		free(code);
		fclose(f);
		return 1;
	}
	fclose(f);

	VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)size, .pCode = code};
	VkResult res = vkCreateShaderModule(h->device, &info, NULL, out);
	free(code);
	VK_TRY(res, "vkCreateShaderModule");
	return 0;
}

static int harness_init(Harness *h)
{
	memset(h, 0, sizeof(*h));

	VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "criticality_test", .apiVersion = VK_API_VERSION_1_1};
	VkInstanceCreateInfo instInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
	VK_TRY(vkCreateInstance(&instInfo, NULL, &h->instance), "vkCreateInstance");

	uint32_t count = 0;
	vkEnumeratePhysicalDevices(h->instance, &count, NULL);
	if (count == 0) {
		fprintf(stderr, "no Vulkan physical devices\n");
		return 1;
	}
	VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * count);
	vkEnumeratePhysicalDevices(h->instance, &count, devices);
	h->physical = devices[0];
	free(devices);

	uint32_t family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(h->physical, &family_count, NULL);
	VkQueueFamilyProperties *families = malloc(sizeof(VkQueueFamilyProperties) * family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(h->physical, &family_count, families);
	h->queue_family = UINT32_MAX;
	for (uint32_t i = 0; i < family_count; i++)
		if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			h->queue_family = i;
			break;
		}
	free(families);
	if (h->queue_family == UINT32_MAX) {
		fprintf(stderr, "no compute queue family\n");
		return 1;
	}

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = h->queue_family, .queueCount = 1, .pQueuePriorities = &priority};
	VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueInfo};
	VK_TRY(vkCreateDevice(h->physical, &deviceInfo, NULL, &h->device), "vkCreateDevice");
	vkGetDeviceQueue(h->device, h->queue_family, 0, &h->queue);

	VkDescriptorSetLayoutBinding bindings[BINDING_COUNT];
	for (uint32_t i = 0; i < BINDING_COUNT; i++)
		bindings[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = BINDING_COUNT, .pBindings = bindings};
	VK_TRY(vkCreateDescriptorSetLayout(h->device, &layoutInfo, NULL, &h->set_layout), "vkCreateDescriptorSetLayout");

	VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(CritPushConstants)};
	VkPipelineLayoutCreateInfo pipeLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &h->set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
	VK_TRY(vkCreatePipelineLayout(h->device, &pipeLayoutInfo, NULL, &h->pipeline_layout), "vkCreatePipelineLayout");

	VkShaderModule module = VK_NULL_HANDLE;
	if (load_shader(h, CRITICALITY_TEST_SHADER_PATH, &module) != 0)
		return 1;
	VkPipelineShaderStageCreateInfo stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"};
	VkComputePipelineCreateInfo pipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = stage, .layout = h->pipeline_layout};
	VkResult res = vkCreateComputePipelines(h->device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &h->pipeline);
	vkDestroyShaderModule(h->device, module, NULL);
	VK_TRY(res, "vkCreateComputePipelines");

	VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BINDING_COUNT};
	VkDescriptorPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &poolSize};
	VK_TRY(vkCreateDescriptorPool(h->device, &poolInfo, NULL, &h->desc_pool), "vkCreateDescriptorPool");

	VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = h->desc_pool, .descriptorSetCount = 1, .pSetLayouts = &h->set_layout};
	VK_TRY(vkAllocateDescriptorSets(h->device, &setInfo, &h->desc_set), "vkAllocateDescriptorSets");

	VkCommandPoolCreateInfo cmdPoolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = h->queue_family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
	VK_TRY(vkCreateCommandPool(h->device, &cmdPoolInfo, NULL, &h->cmd_pool), "vkCreateCommandPool");

	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = h->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VK_TRY(vkAllocateCommandBuffers(h->device, &cmdInfo, &h->cmd_buf), "vkAllocateCommandBuffers");

	return 0;
}

static void harness_destroy(Harness *h)
{
	for (int i = 0; i < BINDING_COUNT; i++) {
		if (h->buffers[i])
			vkDestroyBuffer(h->device, h->buffers[i], NULL);
		if (h->memories[i])
			vkFreeMemory(h->device, h->memories[i], NULL);
	}
	if (h->cmd_pool)
		vkDestroyCommandPool(h->device, h->cmd_pool, NULL);
	if (h->desc_pool)
		vkDestroyDescriptorPool(h->device, h->desc_pool, NULL);
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

// Buffer indices, matching the shader's binding numbers
enum { BUF_OUT_NODES = 0, BUF_OUT_EDGES, BUF_IN_NODES, BUF_IN_EDGES, BUF_LEVELS, BUF_LNW, BUF_LNX, BUF_HEIGHT, BUF_DEPTH, BUF_DISPLAY_EDGES, BUF_DISPLAY_MAX };

static int harness_upload_graph(Harness *h)
{
	VkDeviceSize node_size = sizeof(CritNode) * NODE_COUNT;
	VkDeviceSize value_size = sizeof(float) * NODE_COUNT;

	if (create_buffer(h, BUF_OUT_NODES, node_size) || create_buffer(h, BUF_OUT_EDGES, sizeof(CritEdge) * OUT_EDGE_COUNT) || create_buffer(h, BUF_IN_NODES, node_size) || create_buffer(h, BUF_IN_EDGES, sizeof(CritEdge) * IN_EDGE_COUNT) || create_buffer(h, BUF_LEVELS, sizeof(uint32_t) * NODE_COUNT) || create_buffer(h, BUF_LNW, value_size) || create_buffer(h, BUF_LNX, value_size) || create_buffer(h, BUF_HEIGHT, value_size) || create_buffer(h, BUF_DEPTH, value_size) || create_buffer(h, BUF_DISPLAY_EDGES, sizeof(CritEdge) * OUT_EDGE_COUNT) || create_buffer(h, BUF_DISPLAY_MAX, sizeof(uint32_t)))
		return 1;

	CritEdge out_edges[OUT_EDGE_COUNT] = {0};
	for (int i = 0; i < OUT_EDGE_COUNT; i++) {
		out_edges[i].node = g_out_targets[i];
		out_edges[i].pad = (uint32_t)i;
	}
	CritEdge in_edges[IN_EDGE_COUNT] = {0};
	for (int i = 0; i < IN_EDGE_COUNT; i++)
		in_edges[i].node = g_in_sources[i];

	float zeros[NODE_COUNT] = {0};
	CritEdge display_edges[OUT_EDGE_COUNT] = {0};
	uint32_t display_max = 0;
	if (upload(h, BUF_OUT_NODES, g_out_nodes, node_size) || upload(h, BUF_OUT_EDGES, out_edges, sizeof(out_edges)) || upload(h, BUF_IN_NODES, g_in_nodes, node_size) || upload(h, BUF_IN_EDGES, in_edges, sizeof(in_edges)) || upload(h, BUF_LEVELS, g_level_perm, sizeof(g_level_perm)) || upload(h, BUF_LNW, zeros, value_size) || upload(h, BUF_LNX, zeros, value_size) || upload(h, BUF_HEIGHT, zeros, value_size) || upload(h, BUF_DEPTH, zeros, value_size) || upload(h, BUF_DISPLAY_EDGES, display_edges, sizeof(display_edges)) || upload(h, BUF_DISPLAY_MAX, &display_max, sizeof(display_max)))
		return 1;

	VkDescriptorBufferInfo infos[BINDING_COUNT];
	VkWriteDescriptorSet writes[BINDING_COUNT];
	for (uint32_t i = 0; i < BINDING_COUNT; i++) {
		infos[i] = (VkDescriptorBufferInfo){h->buffers[i], 0, VK_WHOLE_SIZE};
		writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = h->desc_set, .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &infos[i]};
	}
	vkUpdateDescriptorSets(h->device, BINDING_COUNT, writes, 0, NULL);
	return 0;
}

// Records and submits the four sweeps in the same order the renderer does:
// path counts first (heights and depths read them), each level separated by a
// memory barrier, forward stages ascending and backward stages descending.
static int harness_run(Harness *h, uint32_t weight_mode)
{
	VK_TRY(vkResetCommandBuffer(h->cmd_buf, 0), "vkResetCommandBuffer");
	VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_TRY(vkBeginCommandBuffer(h->cmd_buf, &begin), "vkBeginCommandBuffer");

	vkCmdBindPipeline(h->cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, h->pipeline);
	vkCmdBindDescriptorSets(h->cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, h->pipeline_layout, 0, 1, &h->desc_set, 0, NULL);

	VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	const uint32_t stages[4] = {CRIT_STAGE_LNW, CRIT_STAGE_LNX, CRIT_STAGE_HEIGHT, CRIT_STAGE_DEPTH};
	const int ascending[4] = {1, 0, 1, 0};

	for (int s = 0; s < 4; s++) {
		for (int i = 0; i < NUM_LEVELS; i++) {
			int l = ascending[s] ? i : NUM_LEVELS - 1 - i;
			CritPushConstants pc = {g_level_offsets[l], g_level_sizes[l], stages[s], weight_mode};
			vkCmdPushConstants(h->cmd_buf, h->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
			vkCmdDispatch(h->cmd_buf, (pc.num_nodes_in_level + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1, 1);
			vkCmdPipelineBarrier(h->cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
		}
	}

	VK_TRY(vkEndCommandBuffer(h->cmd_buf), "vkEndCommandBuffer");

	VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &h->cmd_buf};
	VK_TRY(vkQueueSubmit(h->queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
	VK_TRY(vkQueueWaitIdle(h->queue), "vkQueueWaitIdle");
	return 0;
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

static const char *g_labels[NODE_COUNT] = {"S", "A", "B", "C", "D", "E", "T", "F"};

static int check_array(const char *what, const float *got, const float *expected)
{
	int failures = 0;
	for (int v = 0; v < NODE_COUNT; v++) {
		if (fabsf(got[v] - expected[v]) > TOLERANCE) {
			fprintf(stderr, "  %s[%s]: got %.6f, expected %.6f\n", what, g_labels[v], (double)got[v], (double)expected[v]);
			failures++;
		}
	}
	if (failures == 0)
		printf("  %s: ok\n", what);
	return failures;
}

static int check_criticality(const char *what, const float *height, const float *depth, const float *expected)
{
	float H = 0.0f;
	for (int v = 0; v < NODE_COUNT; v++)
		if (height[v] + depth[v] > H)
			H = height[v] + depth[v];

	float c[NODE_COUNT];
	for (int v = 0; v < NODE_COUNT; v++) {
		c[v] = H - height[v] - depth[v];
		if (c[v] < 0.0f)
			c[v] = 0.0f;
	}
	printf("  %s: H = %.6f\n", what, (double)H);
	return check_array(what, c, expected);
}

static int run_mode(Harness *h, uint32_t weight_mode, const char *name, const float *expect_height, const float *expect_depth, const float *expect_criticality)
{
	printf("%s weights:\n", name);
	if (harness_run(h, weight_mode) != 0)
		return 1;

	float lnw[NODE_COUNT], lnx[NODE_COUNT], height[NODE_COUNT], depth[NODE_COUNT];
	if (download(h, BUF_LNW, lnw, sizeof(lnw)) || download(h, BUF_LNX, lnx, sizeof(lnx)) || download(h, BUF_HEIGHT, height, sizeof(height)) || download(h, BUF_DEPTH, depth, sizeof(depth)))
		return 1;

	int failures = 0;
	failures += check_array("lnW", lnw, g_expect_lnw);
	failures += check_array("lnX", lnx, g_expect_lnx);
	failures += check_array("height", height, expect_height);
	failures += check_array("depth", depth, expect_depth);
	failures += check_criticality("criticality", height, depth, expect_criticality);
	return failures;
}

static float softplus(float value)
{
	return value > 20.0f ? value : logf(1.0f + expf(value));
}

static int check_live_weights(Harness *h, uint32_t weight_mode, const char *name)
{
	DisplayEdge zero_edges[OUT_EDGE_COUNT] = {0};
	uint32_t zero_max = 0;
	if (upload(h, BUF_DISPLAY_EDGES, zero_edges, sizeof(zero_edges)) || upload(h, BUF_DISPLAY_MAX, &zero_max, sizeof(zero_max)))
		return 1;
	if (harness_run(h, weight_mode) != 0)
		return 1;
	DisplayEdge edges[OUT_EDGE_COUNT];
	uint32_t max_bits = 0;
	if (download(h, BUF_DISPLAY_EDGES, edges, sizeof(edges)) || download(h, BUF_DISPLAY_MAX, &max_bits, sizeof(max_bits)))
		return 1;
	int failures = 0;
	float expected_max = 0.0f;
	for (int e = 0; e < OUT_EDGE_COUNT; e++) {
		float score = g_expect_lnw[g_out_sources[e]];
		if (weight_mode != CRIT_WEIGHT_UNIT)
			score += g_expect_lnx[g_out_targets[e]];
		float expected = weight_mode == CRIT_WEIGHT_SPE ? 1.0f + score : softplus(score);
		if (fabsf(edges[e].weight - expected) > TOLERANCE) {
			fprintf(stderr, "  %s display[%d]: got %.6f, expected %.6f\n", name, e, (double)edges[e].weight, (double)expected);
			failures++;
		}
		if (expected > expected_max)
			expected_max = expected;
	}
	float max_weight = 0.0f;
	memcpy(&max_weight, &max_bits, sizeof(max_weight));
	if (fabsf(max_weight - expected_max) > TOLERANCE) {
		fprintf(stderr, "  %s display max: got %.6f, expected %.6f\n", name, (double)max_weight, (double)expected_max);
		failures++;
	}
	if (failures == 0)
		printf("  %s live edge display: ok\n", name);
	return failures;
}

int main(void)
{
	Harness h;
	if (harness_init(&h) != 0) {
		harness_destroy(&h);
		return 1;
	}
	if (harness_upload_graph(&h) != 0) {
		harness_destroy(&h);
		return 1;
	}

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(h.physical, &props);
	printf("device: %s\n", props.deviceName);

	int failures = 0;
	failures += run_mode(&h, CRIT_WEIGHT_UNIT, "unit", g_expect_unit_height, g_expect_unit_depth, g_expect_unit_criticality);
	failures += run_mode(&h, CRIT_WEIGHT_SPE, "SPE", g_expect_spe_height, g_expect_spe_depth, g_expect_spe_criticality);
	failures += check_live_weights(&h, CRIT_WEIGHT_UNIT, "SPLC");
	failures += check_live_weights(&h, CRIT_WEIGHT_SPC, "SPC");
	failures += check_live_weights(&h, CRIT_WEIGHT_SPE, "SPE");

	harness_destroy(&h);

	if (failures > 0) {
		fprintf(stderr, "criticality_test: %d mismatches\n", failures);
		return 1;
	}
	printf("criticality_test: all checks passed\n");
	return 0;
}
