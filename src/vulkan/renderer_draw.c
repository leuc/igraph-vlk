/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_draw.h"

#include <stdio.h>
#include <string.h>

#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"
#include "vulkan/renderer_compute.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/utils.h"

#include "graph/graph_types.h"

void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index, bool has_ray, vec3 ray_origin, vec3 ray_dir)
{
	uint32_t ubo_idx = r->commands.currentFrame * MAX_VIEWS + view_index;
	UniformBufferObject eye_ubo = r->ubo.data;
	glm_mat4_copy(view, eye_ubo.view);
	glm_mat4_copy(proj, eye_ubo.proj);
	memcpy(r->ubo.mapped[ubo_idx], &eye_ubo, sizeof(UniformBufferObject));
	renderer_anim_upload(r, ubo_idx);

	VkClearValue cv[2];
	cv[0].color = (VkClearColorValue){0.01f, 0.01f, 0.02f, 1.0f};
	cv[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};
	VkRenderPassBeginInfo rpi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp, .framebuffer = fb, .renderArea = {{0, 0}, {extent.width, extent.height}}, .clearValueCount = 2, .pClearValues = cv};
	vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
	vkCmdSetViewport(cmd, 0, 1, &vp);
	VkRect2D sc = {{0, 0}, {extent.width, extent.height}};
	vkCmdSetScissor(cmd, 0, 1, &sc);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.sets[ubo_idx], 0, NULL);

	if (r->showEdges && r->edge.count > 0) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.edge);
		// Push segments per edge for edge index calculation
		int segments = (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) ? EDGE_WIDTH_LINES : 15;
		uint32_t segs = (uint32_t)segments;
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4) * 2, sizeof(uint32_t), &segs);
		VkBuffer prevEdgeBuf = (r->transition.active && r->transition.prev_edge_position != VK_NULL_HANDLE) ? r->transition.prev_edge_position : r->edge.position;
		VkBuffer eBs[] = {r->edge.position, r->edge.attribute, prevEdgeBuf};
		VkDeviceSize eOs[] = {0, 0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 3, eBs, eOs);
		vkCmdDraw(cmd, r->edge.vertex_count, 1, 0, 0);
	}
	if (r->showNodes && r->node.count > 0) {
		float a = 1.0f;
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &a);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.node);
		VkBuffer prevNodeBuf = (r->transition.active && r->transition.prev_node_position != VK_NULL_HANDLE) ? r->transition.prev_node_position : r->node.position;
		VkBuffer vbs[] = {r->nodeVertexBuffer, r->node.position, r->node.attribute, prevNodeBuf};
		VkDeviceSize vos[] = {0, 0, 0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 4, vbs, vos);
		vkCmdDraw(cmd, 3, r->node.count, 0, 0);
	}
	// Node labels (opaque, depth-writing) — draw before menu so menu occludes them
	if (r->label.count > 0 && r->label.instance != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.label);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.node_label_sets[ubo_idx], 0, NULL);
		VkBuffer nVs[] = {r->menu.quad_vertex, r->label.instance};
		VkDeviceSize nOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, nVs, nOs);
		vkCmdBindIndexBuffer(cmd, r->menu.quad_index, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, 6, r->label.count, 0, 0, 0);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.sets[ubo_idx], 0, NULL);
	}
	// Detail card (single instance, dedicated atlas)
	if (r->detail.visible && r->detail.instance != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.label);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.detail_card_sets[ubo_idx], 0, NULL);
		VkBuffer dVs[] = {r->menu.quad_vertex, r->detail.instance};
		VkDeviceSize dOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, dVs, dOs);
		vkCmdBindIndexBuffer(cmd, r->menu.quad_index, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.sets[ubo_idx], 0, NULL);
	}
	if (r->menu.node_count > 0 && r->menu.instance != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.menu);
		VkBuffer mVs[] = {r->menu.quad_vertex, r->menu.instance};
		VkDeviceSize mOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, mVs, mOs);
		vkCmdBindIndexBuffer(cmd, r->menu.quad_index, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, r->menu.quad_index_count, r->menu.node_count, 0, 0, 0);
	}
	if (r->menu.text_quad_instance_count > 0 && r->menu.text_quad_instance != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.textQuad);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.text_quad_sets[ubo_idx], 0, NULL);
		VkBuffer tVs[] = {r->menu.quad_vertex, r->menu.text_quad_instance};
		VkDeviceSize tOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, tVs, tOs);
		vkCmdBindIndexBuffer(cmd, r->menu.quad_index, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, 6, r->menu.text_quad_instance_count, 0, 0, 0);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptors.sets[ubo_idx], 0, NULL);
	}
	if (r->showUI) {
		float viewportSize[2] = {(float)extent.width, (float)extent.height};
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4) * 2 + sizeof(float) + sizeof(uint32_t), sizeof(float) * 2, viewportSize);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.ui);
		VkBuffer bVs[] = {r->uiBgVertexBuffer, r->uiBgInstanceBuffer};
		VkDeviceSize bOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, bVs, bOs);
		vkCmdDraw(cmd, 4, 1, 0, 0);
		if (r->uiTextCharCount > 0) {
			VkBuffer tVs[] = {r->labelVertexBuffer, r->uiTextInstanceBuffer};
			VkDeviceSize tOs[] = {0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 2, tVs, tOs);
			vkCmdDraw(cmd, 4, r->uiTextCharCount, 0, 0);
		}
	}
	if (has_ray)
		renderer_render_ray(r, cmd, ray_origin, ray_dir, view, proj);
	vkCmdEndRenderPass(cmd);
}

void renderer_draw_frame(Renderer *r, GraphData *graph)
{
	if (r->framebufferResized) {
		renderer_recreate_swapchain(r);
		return;
	}
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->commands.inFlightFences[r->commands.currentFrame], VK_TRUE, UINT64_MAX), "Failed to wait for in-flight fences");

	// SPLC readback: sync GPU edge weights to host graph after animation completes
	if (r->splc.readback_pending) {
		renderer_readback_splc_weights(r, graph);
		r->splc.readback_pending = false;
		r->splc.active = false;
	}

	uint32_t imageIndex;
	VkResult res = vkAcquireNextImageKHR(r->core.device, r->swapchain.swapchain, UINT64_MAX, r->commands.imageAvailableSemaphores[r->commands.currentFrame], VK_NULL_HANDLE, &imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
		renderer_recreate_swapchain(r);
		return;
	} else if (res != VK_SUCCESS) {
		fprintf(stderr, "Failed to acquire swapchain image\n");
		return;
	}

	VK_CHECK(vkResetFences(r->core.device, 1, &r->commands.inFlightFences[r->commands.currentFrame]), "Failed to reset in-flight fences");
	VK_CHECK(vkResetCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame], 0), "Failed to reset command buffer");
	VK_CHECK(vkBeginCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame], &VK_CMD_BEGIN_INFO), "Failed to begin command buffer");

	// SPLC animation: advance one level every splc_level_interval seconds
	if (r->splc.active) {
		double now = (double)r->anim.data.time;
		if (now - r->splc.last_level_time >= r->splc.level_interval) {
			renderer_dispatch_splc_level(r, r->commands.commandBuffers[r->commands.currentFrame]);
		}
	}

	renderer_render_scene(r, r->commands.commandBuffers[r->commands.currentFrame], r->renderPass.renderPass, r->renderPass.framebuffers[imageIndex], r->swapchain.extent, r->ubo.data.view, r->ubo.data.proj, 0, false, (vec3){0}, (vec3){0});
	VK_CHECK(vkEndCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame]), "Failed to end command buffer");

	VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->commands.imageAvailableSemaphores[r->commands.currentFrame], .pWaitDstStageMask = &waitStages, .commandBufferCount = 1, .pCommandBuffers = &r->commands.commandBuffers[r->commands.currentFrame], .signalSemaphoreCount = 1, .pSignalSemaphores = &r->commands.renderFinishedSemaphores[imageIndex]};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->commands.inFlightFences[r->commands.currentFrame]), "Failed to submit draw command buffer");

	VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->commands.renderFinishedSemaphores[imageIndex], .swapchainCount = 1, .pSwapchains = &r->swapchain.swapchain, .pImageIndices = &imageIndex};
	VK_CHECK(vkQueuePresentKHR(r->core.presentQueue, &presentInfo), "Failed to present swapchain image");
	r->commands.currentFrame = (r->commands.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
