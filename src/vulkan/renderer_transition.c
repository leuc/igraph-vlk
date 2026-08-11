/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_transition.h"
#include "vulkan/buffers.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"
#include <cglm/cglm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMOOTHSTEP(t) ((t) * (t) * (3.0f - 2.0f * (t)))

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

// First transition for this owner: snapshot current GPU positions -> prev buffer.
static void transition_start(Renderer *r, TransitionSource source, float duration)
{
	uint32_t ncount = r->node.count;
	uint32_t ecount = r->edge.vertex_count;
	VkDeviceSize node_size = sizeof(NodePosition) * (ncount > 0 ? ncount : 1);
	VkDeviceSize edge_size = sizeof(EdgePosition) * (ecount > 0 ? ecount : 1);

	if (r->transition.prev_node_capacity < ncount || r->transition.prev_node_position == VK_NULL_HANDLE) {
		destroy_prev_node_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, node_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_node_position, &r->transition.prev_node_position_memory);
		r->transition.prev_node_capacity = ncount;
	}

	if (ncount > 0 && r->node.position != VK_NULL_HANDLE) {
		void *src_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->node.position_memory, 0, node_size, 0, &src_mapped), "transition start map src");
		void *dst_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, node_size, 0, &dst_mapped), "transition start map dst");
		memcpy(dst_mapped, src_mapped, sizeof(NodePosition) * ncount);
		vkUnmapMemory(r->core.device, r->node.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
	}
	r->transition.prev_node_count = ncount;

	r->transition.t = 0.0f;
	r->transition.duration = duration;
	r->transition.active = true;
	r->transition.owner = source;
	r->transition.owner_generation++;

	if (r->transition.prev_edge_capacity < ecount || r->transition.prev_edge_position == VK_NULL_HANDLE) {
		destroy_prev_edge_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edge_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_edge_position, &r->transition.prev_edge_position_memory);
		r->transition.prev_edge_capacity = ecount;
	}

	if (ecount > 0 && r->edge.position != VK_NULL_HANDLE) {
		void *src_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->edge.position_memory, 0, edge_size, 0, &src_mapped), "transition start edge map src");
		void *dst_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, edge_size, 0, &dst_mapped), "transition start edge map dst");
		memcpy(dst_mapped, src_mapped, sizeof(EdgePosition) * ecount);
		vkUnmapMemory(r->core.device, r->edge.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
	}
	r->transition.prev_edge_vertex_count = ecount;
}

// Retarget by the current owner: snapshot the currently-displayed (curr)
// positions as the new prev baseline, so the new target morphs from where
// the visuals actually are, instead of resetting progress.
static void transition_retarget(Renderer *r, float duration)
{
	uint32_t ncount = r->node.count;
	uint32_t ecount = r->edge.vertex_count;
	VkDeviceSize node_size = sizeof(NodePosition) * (ncount > 0 ? ncount : 1);
	VkDeviceSize edge_size = sizeof(EdgePosition) * (ecount > 0 ? ecount : 1);

	if (r->transition.prev_node_capacity < ncount || r->transition.prev_node_position == VK_NULL_HANDLE) {
		destroy_prev_node_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, node_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_node_position, &r->transition.prev_node_position_memory);
		r->transition.prev_node_capacity = ncount;
	}

	if (ncount > 0 && r->node.position != VK_NULL_HANDLE) {
		void *curr_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->node.position_memory, 0, node_size, 0, &curr_mapped), "transition retarget map curr");
		void *prev_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, node_size, 0, &prev_mapped), "transition retarget map prev");
		memcpy(prev_mapped, curr_mapped, sizeof(NodePosition) * ncount);
		vkUnmapMemory(r->core.device, r->node.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
	}

	r->transition.prev_node_count = ncount;
	r->transition.duration = duration;
	r->transition.owner_generation++;

	if (r->transition.prev_edge_capacity < ecount || r->transition.prev_edge_position == VK_NULL_HANDLE) {
		destroy_prev_edge_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edge_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_edge_position, &r->transition.prev_edge_position_memory);
		r->transition.prev_edge_capacity = ecount;
	}

	if (ecount > 0 && r->edge.position != VK_NULL_HANDLE) {
		void *curr_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->edge.position_memory, 0, edge_size, 0, &curr_mapped), "transition retarget map curr edge");
		void *prev_mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, edge_size, 0, &prev_mapped), "transition retarget map prev edge");
		memcpy(prev_mapped, curr_mapped, sizeof(EdgePosition) * ecount);
		vkUnmapMemory(r->core.device, r->edge.position_memory);
		vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
	}

	r->transition.prev_edge_vertex_count = ecount;
}

// Edges have no stable identity (rebuilt from node positions each frame). If
// the vertex count changed, snap edge prev to target -- can't lerp across
// different topologies.
static void transition_reconcile(Renderer *r, GraphData *graph)
{
	(void)graph;
	uint32_t new_node_count = r->node.count;

	if (r->transition.prev_node_count < new_node_count) {
		uint32_t old_count = r->transition.prev_node_count;
		VkDeviceSize old_size = sizeof(NodePosition) * (old_count > 0 ? old_count : 1);
		VkDeviceSize new_size = sizeof(NodePosition) * (new_node_count > 0 ? new_node_count : 1);

		NodePosition *old_prev = NULL;
		if (old_count > 0 && r->transition.prev_node_position != VK_NULL_HANDLE) {
			old_prev = malloc(old_size);
			void *old_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, old_size, 0, &old_mapped), "transition reconcile read old");
			memcpy(old_prev, old_mapped, old_size);
			vkUnmapMemory(r->core.device, r->transition.prev_node_position_memory);
		}

		destroy_prev_node_position(r);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, new_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_node_position, &r->transition.prev_node_position_memory);
		r->transition.prev_node_capacity = new_node_count;

		if (new_node_count > 0 && r->node.position != VK_NULL_HANDLE) {
			void *curr_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->node.position_memory, 0, new_size, 0, &curr_mapped), "transition reconcile map curr");
			void *prev_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_node_position_memory, 0, new_size, 0, &prev_mapped), "transition reconcile map prev");

			vec3 *curr = (vec3 *)curr_mapped;
			vec3 *prev = (vec3 *)prev_mapped;

			if (old_prev) {
				for (uint32_t i = 0; i < old_count; i++) {
					prev[i][0] = old_prev[i].pos[0];
					prev[i][1] = old_prev[i].pos[1];
					prev[i][2] = old_prev[i].pos[2];
				}
			}
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

	uint32_t new_edge_vertex_count = r->edge.vertex_count;
	if (r->transition.prev_edge_vertex_count < new_edge_vertex_count) {
		uint32_t old_ecount = r->transition.prev_edge_vertex_count;
		VkDeviceSize old_esize = sizeof(EdgePosition) * (old_ecount > 0 ? old_ecount : 1);
		VkDeviceSize new_esize = sizeof(EdgePosition) * (new_edge_vertex_count > 0 ? new_edge_vertex_count : 1);

		EdgePosition *old_edge_prev = NULL;
		if (old_ecount > 0 && r->transition.prev_edge_position != VK_NULL_HANDLE) {
			old_edge_prev = malloc(old_esize);
			void *old_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, old_esize, 0, &old_mapped), "transition edge reconcile read old");
			memcpy(old_edge_prev, old_mapped, old_esize);
			vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
		}

		if (r->transition.prev_edge_capacity < new_edge_vertex_count) {
			destroy_prev_edge_position(r);
			VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, new_esize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->transition.prev_edge_position, &r->transition.prev_edge_position_memory);
			r->transition.prev_edge_capacity = new_edge_vertex_count;
		}

		if (new_edge_vertex_count > 0 && r->edge.position != VK_NULL_HANDLE) {
			void *curr_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->edge.position_memory, 0, new_esize, 0, &curr_mapped), "transition edge reconcile map curr");
			void *prev_mapped;
			VK_CHECK(vkMapMemory(r->core.device, r->transition.prev_edge_position_memory, 0, new_esize, 0, &prev_mapped), "transition edge reconcile map prev");

			vec3 *curr = (vec3 *)curr_mapped;
			vec3 *prev = (vec3 *)prev_mapped;

			if (old_edge_prev) {
				for (uint32_t i = 0; i < old_ecount; i++) {
					prev[i][0] = old_edge_prev[i].pos[0];
					prev[i][1] = old_edge_prev[i].pos[1];
					prev[i][2] = old_edge_prev[i].pos[2];
				}
			}
			for (uint32_t i = old_ecount; i < new_edge_vertex_count; i++) {
				glm_vec3_copy(curr[i], prev[i]);
			}

			vkUnmapMemory(r->core.device, r->edge.position_memory);
			vkUnmapMemory(r->core.device, r->transition.prev_edge_position_memory);
		}

		free(old_edge_prev);
		r->transition.prev_edge_vertex_count = new_edge_vertex_count;
	} else if (r->transition.prev_edge_vertex_count > new_edge_vertex_count) {
		r->transition.prev_edge_vertex_count = new_edge_vertex_count;
	}
}

// Single entry point for callers that want their graph update to (optionally)
// morph in. Ownership: a transition has one owner (by source). A request from
// the current owner retargets in place; a request from a different source
// while one is active is parked in a depth-1 pending slot (newest wins) and
// only applied once the active transition completes -- no cross-source blend.
void renderer_transition_request(Renderer *r, TransitionSource source, float duration, GraphData *graph)
{
	if (duration <= 0.0f) {
		renderer_update_graph(r, graph);
		return;
	}

	if (!r->transition.active) {
		transition_start(r, source, duration);
		renderer_update_graph(r, graph);
		transition_reconcile(r, graph);
		r->transition.has_pending = false;
		return;
	}

	if (r->transition.owner == source) {
		transition_retarget(r, duration);
		renderer_update_graph(r, graph);
		transition_reconcile(r, graph);
		return;
	}

	r->transition.has_pending = true;
	r->transition.pending_source = source;
	r->transition.pending_duration = duration;
	r->transition.pending_graph = graph;
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
		r->transition.owner = TRANSITION_SOURCE_NONE;
	}

	float eased_t = SMOOTHSTEP(r->transition.t);
	r->anim.data.transition_t = eased_t;

	if (!r->transition.active && r->transition.has_pending) {
		TransitionSource src = r->transition.pending_source;
		float dur = r->transition.pending_duration;
		GraphData *graph = r->transition.pending_graph;
		r->transition.has_pending = false;
		r->transition.pending_graph = NULL;
		renderer_transition_request(r, src, dur, graph);
	}
}
