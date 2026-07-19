#include "vulkan/renderer_transition.h"
#include "vulkan/buffers.h"
#include "vulkan/utils.h"
#include <cglm/cglm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMOOTHSTEP(t) ((t) * (t) * (3.0f - 2.0f * (t)))

#define TRANSITION_DEFAULT_DURATION 0.4f
#define STREAM_TRANSITION_DURATION 0.12f

void renderer_transition_init(Renderer *r)
{
	memset(&r->transition, 0, sizeof(TransitionState));
}

static void destroy_prev_node_position(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->transition.prev_node_position, r->transition.prev_node_position_memory);
	r->transition.prev_node_count = 0;
}

static void destroy_prev_edge_position(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->transition.prev_edge_position, r->transition.prev_edge_position_memory);
	r->transition.prev_edge_vertex_count = 0;
}

void renderer_transition_cleanup(Renderer *r)
{
	destroy_prev_node_position(r);
	destroy_prev_edge_position(r);
}

bool renderer_transition_is_active(Renderer *r)
{
	return r->transition.active;
}

void renderer_transition_begin(Renderer *r, float duration)
{
	if (duration <= 0.0f) {
		duration = TRANSITION_DEFAULT_DURATION;
	}

	uint32_t ncount = r->node.count;
	uint32_t ecount = r->edge.vertex_count;
	fprintf(stderr, "[Transition] begin: nodes=%u edges=%u duration=%.2fs was_active=%d\n", ncount, ecount, duration, r->transition.active);

	VkDeviceSize node_size = sizeof(NodePosition) * (ncount > 0 ? ncount : 1);
	VkDeviceSize edge_size = sizeof(EdgePosition) * (ecount > 0 ? ecount : 1);

	bool interrupting = r->transition.active && r->transition.prev_node_position != VK_NULL_HANDLE && r->node.position != VK_NULL_HANDLE;

	if (interrupting) {
		// Seamless interruption: advance prev to current interpolated position
		float t = r->transition.t;
		float eased_t = SMOOTHSTEP(t);

		uint32_t lerp_count = ncount < r->transition.prev_node_count ? ncount : r->transition.prev_node_count;

		// Handle buffer resize: read old prev into temp before destroy
		NodePosition *old_prev = NULL;
		uint32_t old_prev_count = r->transition.prev_node_count;
		if (old_prev_count > 0 && r->transition.prev_node_position != VK_NULL_HANDLE) {
			old_prev = malloc(sizeof(NodePosition) * old_prev_count);
			void *old_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, sizeof(NodePosition) * old_prev_count, 0, &old_mapped), "transition interrupt read old");
			memcpy(old_prev, old_mapped, sizeof(NodePosition) * old_prev_count);
			vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
		}

		// Grow prev buffer if needed
		if (r->transition.prev_node_capacity < ncount || r->transition.prev_node_position == VK_NULL_HANDLE) {
			destroy_prev_node_position(r);
			VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, node_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_node_position, &r->transition.prev_node_position_memory);
			r->transition.prev_node_capacity = ncount;
		}

		// Write interpolated positions: for existing nodes, lerp old_prev → curr
		if (ncount > 0 && r->node.position != VK_NULL_HANDLE) {
			void *curr_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->node.position_memory, 0, node_size, 0, &curr_mapped), "transition interrupt map curr");
			void *prev_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, node_size, 0, &prev_mapped), "transition interrupt map prev");

			vec3 *curr = (vec3 *)curr_mapped;
			vec3 *prev = (vec3 *)prev_mapped;

			// Lerp existing nodes: prev = interpolated position
			for (uint32_t i = 0; i < lerp_count && old_prev; i++) {
				prev[i][0] = old_prev[i].pos[0];
				prev[i][1] = old_prev[i].pos[1];
				prev[i][2] = old_prev[i].pos[2];
				glm_vec3_lerp(prev[i], curr[i], eased_t, prev[i]);
			}
			// New nodes (if count grew): appear in place
			for (uint32_t i = lerp_count; i < ncount; i++) {
				glm_vec3_copy(curr[i], prev[i]);
			}

			vkUnmapMemory(r->core.device, r->node.position_memory);
			vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
		}

		free(old_prev);
		r->transition.prev_node_count = ncount;

		// Don't reset t — continue from current progress toward the new target
		// Just update duration if needed
		r->transition.duration = duration;
		fprintf(stderr, "[Transition] interrupt: kept t=%.3f\n", r->transition.t);
	} else {
		// First transition: snapshot current GPU positions → prev buffer
		if (r->transition.prev_node_capacity < ncount || r->transition.prev_node_position == VK_NULL_HANDLE) {
			destroy_prev_node_position(r);
			VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, node_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_node_position, &r->transition.prev_node_position_memory);
			r->transition.prev_node_capacity = ncount;
		}

		if (ncount > 0 && r->node.position != VK_NULL_HANDLE) {
			void *src_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->node.position_memory, 0, node_size, 0, &src_mapped), "transition begin map src");
			void *dst_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, node_size, 0, &dst_mapped), "transition begin map dst");
			memcpy(dst_mapped, src_mapped, sizeof(NodePosition) * ncount);
			vkUnmapMemory(r->core.device, r->node.position_memory);
			vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
		}
		r->transition.prev_node_count = ncount;

		r->transition.t = 0.0f;
		r->transition.duration = duration;
		r->transition.active = true;
	}

	// Edge prev buffer: always snapshot current GPU positions into prev.
	// Edges are rebuilt from node positions each frame (no stable identity),
	// so we always take the latest snapshot as the "from" for the lerp.
	if (r->transition.prev_edge_capacity < ecount || r->transition.prev_edge_position == VK_NULL_HANDLE) {
		destroy_prev_edge_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edge_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_edge_position, &r->transition.prev_edge_position_memory);
		r->transition.prev_edge_capacity = ecount;
	}

	if (ecount > 0 && r->edge.position != VK_NULL_HANDLE) {
		void *src_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->edge.position_memory, 0, edge_size, 0, &src_mapped), "transition edge begin map src");
		void *dst_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, edge_size, 0, &dst_mapped), "transition edge begin map dst");
		memcpy(dst_mapped, src_mapped, sizeof(EdgePosition) * ecount);
		vkUnmapMemory(r->core.device, r->edge.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
	}
	r->transition.prev_edge_vertex_count = ecount;
}

void renderer_transition_reconcile(Renderer *r, GraphData *graph)
{
	if (!r->transition.active)
		return;

	uint32_t new_node_count = r->node.count;
	fprintf(stderr, "[Transition] reconcile: prev_nodes=%u new_nodes=%u prev_edges=%u new_edges=%u\n", r->transition.prev_node_count, new_node_count, r->transition.prev_edge_vertex_count, r->edge.vertex_count);

	// Reconcile node prev buffer
	if (r->transition.prev_node_count < new_node_count) {
		uint32_t old_count = r->transition.prev_node_count;
		VkDeviceSize old_size = sizeof(NodePosition) * (old_count > 0 ? old_count : 1);
		VkDeviceSize new_size = sizeof(NodePosition) * (new_node_count > 0 ? new_node_count : 1);

		// Read old prev data before destroying
		NodePosition *old_prev = NULL;
		if (old_count > 0 && r->transition.prev_node_position != VK_NULL_HANDLE) {
			old_prev = malloc(old_size);
			void *old_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, old_size, 0, &old_mapped), "transition reconcile read old");
			memcpy(old_prev, old_mapped, old_size);
			vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
		}

		// Reallocate
		destroy_prev_node_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, new_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_node_position, &r->transition.prev_node_position_memory);
		r->transition.prev_node_capacity = new_node_count;

		// Map new buffer
		if (new_node_count > 0 && r->node.position != VK_NULL_HANDLE) {
			void *curr_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->node.position_memory, 0, new_size, 0, &curr_mapped), "transition reconcile map curr");
			void *prev_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, new_size, 0, &prev_mapped), "transition reconcile map prev");

			vec3 *curr = (vec3 *)curr_mapped;
			vec3 *prev = (vec3 *)prev_mapped;

			// Copy old prev positions for existing nodes
			if (old_prev) {
				for (uint32_t i = 0; i < old_count; i++) {
					prev[i][0] = old_prev[i].pos[0];
					prev[i][1] = old_prev[i].pos[1];
					prev[i][2] = old_prev[i].pos[2];
				}
			}
			// New nodes spawn in place (prev = target)
			for (uint32_t i = old_count; i < new_node_count; i++) {
				glm_vec3_copy(curr[i], prev[i]);
			}

			vkUnmapMemory(r->core.device, r->node.position_memory);
			vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
		}

		free(old_prev);
		r->transition.prev_node_count = new_node_count;
	} else if (r->transition.prev_node_count > new_node_count) {
		r->transition.prev_node_count = new_node_count;
	}

	// For edges: if count changed, set prev = target (snap edges, can't lerp different topologies)
	uint32_t new_edge_vertex_count = r->edge.vertex_count;
	if (r->transition.prev_edge_vertex_count != new_edge_vertex_count && r->edge.position != VK_NULL_HANDLE) {
		VkDeviceSize e_new_size = sizeof(EdgePosition) * new_edge_vertex_count;
		if (r->transition.prev_edge_capacity < new_edge_vertex_count) {
			destroy_prev_edge_position(r);
			VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, e_new_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_edge_position, &r->transition.prev_edge_position_memory);
			r->transition.prev_edge_capacity = new_edge_vertex_count;
		}
		void *curr_e;
		VK_CHECK(vkMapMemory(r->core.device, r->edge.position_memory, 0, e_new_size, 0, &curr_e), "transition edge reconcile map curr");
		void *prev_e;
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, e_new_size, 0, &prev_e), "transition edge reconcile map prev");
		memcpy(prev_e, curr_e, sizeof(EdgePosition) * new_edge_vertex_count);
		vkUnmapMemory(r->core.device, r->edge.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
		r->transition.prev_edge_vertex_count = new_edge_vertex_count;
	}
}

void renderer_transition_update(Renderer *r, float delta_time)
{
	if (!r->transition.active) {
		r->anim.data.transition_t = 1.0f;
		return;
	}

	r->transition.t += delta_time / r->transition.duration;
	if (r->transition.t >= 1.0f) {
		r->transition.t = 1.0f;
		r->transition.active = false;
		fprintf(stderr, "[Transition] complete\n");
	}

	float eased_t = SMOOTHSTEP(r->transition.t);
	r->anim.data.transition_t = eased_t;

	// CPU-lerp edge positions: edges are derived from node positions, so they
	// must interpolate from the same "from" state as nodes. The shader-side
	// edge lerp can't do this because edge prev would be a stale snapshot of
	// old target positions, not the interpolated visual positions.
	uint32_t ecount = r->edge.vertex_count;
	if (ecount > 0 && ecount == r->transition.prev_edge_vertex_count && r->transition.prev_edge_position != VK_NULL_HANDLE && r->edge.position != VK_NULL_HANDLE) {
		VkDeviceSize edge_size = sizeof(EdgePosition) * ecount;
		void *curr_mapped;
		void *prev_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->edge.position_memory, 0, edge_size, 0, &curr_mapped), "transition update map curr edge");
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, edge_size, 0, &prev_mapped), "transition update map prev edge");

		EdgePosition *curr = (EdgePosition *)curr_mapped;
		EdgePosition *prev = (EdgePosition *)prev_mapped;
		for (uint32_t i = 0; i < ecount; i++) {
			curr[i].pos[0] = prev[i].pos[0] + (curr[i].pos[0] - prev[i].pos[0]) * eased_t;
			curr[i].pos[1] = prev[i].pos[1] + (curr[i].pos[1] - prev[i].pos[1]) * eased_t;
			curr[i].pos[2] = prev[i].pos[2] + (curr[i].pos[2] - prev[i].pos[2]) * eased_t;
		}

		vkUnmapMemory(r->core.device, r->edge.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
	}

	fprintf(stderr, "[Transition] update: t=%.3f eased=%.3f active=%d\n", r->transition.t, r->anim.data.transition_t, r->transition.active);
}
