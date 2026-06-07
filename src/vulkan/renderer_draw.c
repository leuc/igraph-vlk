#include "vulkan/renderer_draw.h"

#include <string.h>

#include "vulkan/renderer.h"
#include "vulkan/renderer_compute.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/utils.h"

void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index, bool has_ray, vec3 ray_origin, vec3 ray_dir)
{
	uint32_t ubo_idx = r->commands.currentFrame * MAX_VIEWS + view_index;
	UniformBufferObject eye_ubo = r->ubo;
	glm_mat4_copy(view, eye_ubo.view);
	glm_mat4_copy(proj, eye_ubo.proj);
	memcpy(r->uboMapped[ubo_idx], &eye_ubo, sizeof(UniformBufferObject));

	VkClearValue cv[2];
	cv[0].color = (VkClearColorValue){0.01f, 0.01f, 0.02f, 1.0f};
	cv[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};
	VkRenderPassBeginInfo rpi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp, .framebuffer = fb, .renderArea = {{0, 0}, {extent.width, extent.height}}, .clearValueCount = 2, .pClearValues = cv};
	vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
	vkCmdSetViewport(cmd, 0, 1, &vp);
	VkRect2D sc = {{0, 0}, {extent.width, extent.height}};
	vkCmdSetScissor(cmd, 0, 1, &sc);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[ubo_idx], 0, NULL);

	if (r->showEdges && r->edgeCount > 0) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->edgePipeline);
		// Push SPLC max weight + segments per edge for edge index calculation
		int segments = (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) ? 1 : 15;
		float maxWeight = r->splc_max_weight > 0.0f ? r->splc_max_weight : 1.0f;
		struct
		{
			float maxWeight;
			uint32_t segments;
		} splcPC = {maxWeight, (uint32_t)segments};
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4) * 2, sizeof(splcPC), &splcPC);
		VkBuffer eBs[] = {r->edgePositionBuffer, r->edgeAttributeBuffer};
		VkDeviceSize eOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, eBs, eOs);
		vkCmdDraw(cmd, r->edgeVertexCount, 1, 0, 0);
	}
	if (r->showNodes && r->nodeCount > 0) {
		float a = 1.0f;
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &a);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->nodePipeline);
		for (int i = 0; i < PLATONIC_COUNT; i++) {
			if (r->platonicDrawCalls[i].count == 0)
				continue;
			VkBuffer vbs[] = {r->vertexBuffers[i], r->nodePositionBuffer, r->nodeAttributeBuffer};
			VkDeviceSize vos[] = {0, 0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 3, vbs, vos);
			vkCmdBindIndexBuffer(cmd, r->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, r->platonicIndexCounts[i], r->platonicDrawCalls[i].count, 0, 0, r->platonicDrawCalls[i].firstInstance);
		}
	}
	// Node labels (opaque, depth-writing) — draw before menu so menu occludes them
	if (r->nodeLabelInstanceCount > 0 && r->nodeLabelInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->nodeLabelDescSets[ubo_idx], 0, NULL);
		VkBuffer nVs[] = {r->menuQuadVertexBuffer, r->nodeLabelInstanceBuffer};
		VkDeviceSize nOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, nVs, nOs);
		vkCmdBindIndexBuffer(cmd, r->menuQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, 6, r->nodeLabelInstanceCount, 0, 0, 0);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[ubo_idx], 0, NULL);
	}
	// Detail card (single instance, dedicated atlas)
	if (r->detailCardVisible && r->detailCardInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->detailCardDescSets[ubo_idx], 0, NULL);
		VkBuffer dVs[] = {r->menuQuadVertexBuffer, r->detailCardInstanceBuffer};
		VkDeviceSize dOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, dVs, dOs);
		vkCmdBindIndexBuffer(cmd, r->menuQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[ubo_idx], 0, NULL);
	}
	if (r->menuNodeCount > 0 && r->menuInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->menuPipeline);
		VkBuffer mVs[] = {r->menuQuadVertexBuffer, r->menuInstanceBuffer};
		VkDeviceSize mOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, mVs, mOs);
		vkCmdBindIndexBuffer(cmd, r->menuQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, r->menuQuadIndexCount, r->menuNodeCount, 0, 0, 0);
	}
	if (r->textQuadInstanceCount > 0 && r->textQuadInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->textQuadPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->textQuadDescriptorSets[ubo_idx], 0, NULL);
		VkBuffer tVs[] = {r->menuQuadVertexBuffer, r->textQuadInstanceBuffer};
		VkDeviceSize tOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, tVs, tOs);
		vkCmdBindIndexBuffer(cmd, r->menuQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, 6, r->textQuadInstanceCount, 0, 0, 0);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[ubo_idx], 0, NULL);
	}
	if (r->showUI) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->uiPipeline);
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

void renderer_draw_frame(Renderer *r)
{
	if (r->framebufferResized) {
		renderer_recreate_swapchain(r);
		return;
	}
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->commands.inFlightFences[r->commands.currentFrame], VK_TRUE, UINT64_MAX), "Failed to wait for in-flight fences");
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
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	VK_CHECK(vkBeginCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame], &beginInfo), "Failed to begin command buffer");

	// SPLC animation: advance one level every splc_frames_per_level frames
	if (r->splc_active) {
		r->splc_timer++;
		if (r->splc_timer >= r->splc_frames_per_level) {
			renderer_dispatch_splc_level(r, r->commands.commandBuffers[r->commands.currentFrame]);
			// After all levels are processed, update max_weight for normalization
			if (!r->splc_active) {
				printf("SPLC animation finished (%d levels, max weight heur=%.0f)\n", r->splc_num_levels, r->splc_max_weight);
			}
		}
	}

	renderer_render_scene(r, r->commands.commandBuffers[r->commands.currentFrame], r->renderPass.renderPass, r->renderPass.framebuffers[imageIndex], r->swapchain.extent, r->ubo.view, r->ubo.proj, 0, false, (vec3){0}, (vec3){0});
	VK_CHECK(vkEndCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame]), "Failed to end command buffer");

	VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->commands.imageAvailableSemaphores[r->commands.currentFrame], .pWaitDstStageMask = &waitStages, .commandBufferCount = 1, .pCommandBuffers = &r->commands.commandBuffers[r->commands.currentFrame], .signalSemaphoreCount = 1, .pSignalSemaphores = &r->commands.renderFinishedSemaphores[imageIndex]};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->commands.inFlightFences[r->commands.currentFrame]), "Failed to submit draw command buffer");

	VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->commands.renderFinishedSemaphores[imageIndex], .swapchainCount = 1, .pSwapchains = &r->swapchain.swapchain, .pImageIndices = &imageIndex};
	VK_CHECK(vkQueuePresentKHR(r->core.presentQueue, &presentInfo), "Failed to present swapchain image");
	r->commands.currentFrame = (r->commands.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
