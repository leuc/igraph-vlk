/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/render_pass.h"

#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "vulkan/images.h"
#include "vulkan/utils.h"

#define SCENE_COLOR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

static void create_scene_render_pass(VulkanRenderPass *pass, VulkanCore *core, VulkanSwapchain *swapchain)
{
	VkAttachmentDescription color = {.format = SCENE_COLOR_FORMAT, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkAttachmentDescription depth = {.format = swapchain->depthFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkAttachmentReference color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depth_ref = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color_ref, .pDepthStencilAttachment = &depth_ref};
	VkSubpassDependency dependencies[] = {
		{.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
		{.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT},
	};
	VkAttachmentDescription attachments[] = {color, depth};
	VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = attachments, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 2, .pDependencies = dependencies};
	VK_CHECK(vkCreateRenderPass(core->device, &info, NULL, &pass->renderPass), "Failed to create linear scene render pass");
}

static void create_present_render_pass(VulkanRenderPass *pass, VulkanCore *core, VulkanSwapchain *swapchain)
{
	VkAttachmentDescription color = {.format = swapchain->imageFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
	VkAttachmentReference color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color_ref};
	VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
	VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};
	VK_CHECK(vkCreateRenderPass(core->device, &info, NULL, &pass->presentRenderPass), "Failed to create presentation render pass");
}

void vulkan_render_pass_create(VulkanRenderPass *pass, VulkanCore *core, VulkanSwapchain *swapchain)
{
	pass->renderPass = VK_NULL_HANDLE;
	pass->presentRenderPass = VK_NULL_HANDLE;
	pass->framebuffers = NULL;
	pass->presentFramebuffers = NULL;
	pass->colorImages = NULL;
	pass->colorMemories = NULL;
	pass->colorViews = NULL;
	pass->depthImages = NULL;
	pass->depthMemories = NULL;
	pass->depthViews = NULL;
	pass->imageCount = swapchain->imageCount;

	create_scene_render_pass(pass, core, swapchain);
	create_present_render_pass(pass, core, swapchain);

	uint32_t count = pass->imageCount;
	pass->framebuffers = calloc(count, sizeof(*pass->framebuffers));
	pass->presentFramebuffers = calloc(count, sizeof(*pass->presentFramebuffers));
	pass->colorImages = calloc(count, sizeof(*pass->colorImages));
	pass->colorMemories = calloc(count, sizeof(*pass->colorMemories));
	pass->colorViews = calloc(count, sizeof(*pass->colorViews));
	pass->depthImages = calloc(count, sizeof(*pass->depthImages));
	pass->depthMemories = calloc(count, sizeof(*pass->depthMemories));
	pass->depthViews = calloc(count, sizeof(*pass->depthViews));

	for (uint32_t i = 0; i < count; i++) {
		create_image(core->device, core->physicalDevice, swapchain->extent.width, swapchain->extent.height, SCENE_COLOR_FORMAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &pass->colorImages[i], &pass->colorMemories[i]);
		VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(pass->colorImages[i], SCENE_COLOR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &pass->colorViews[i]), "Failed to create linear scene image view");
		create_image(core->device, core->physicalDevice, swapchain->extent.width, swapchain->extent.height, swapchain->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &pass->depthImages[i], &pass->depthMemories[i]);
		VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(pass->depthImages[i], swapchain->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT), NULL, &pass->depthViews[i]), "Failed to create scene depth image view");

		VkImageView scene_attachments[] = {pass->colorViews[i], pass->depthViews[i]};
		VkFramebufferCreateInfo scene_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass->renderPass, .attachmentCount = 2, .pAttachments = scene_attachments, .width = swapchain->extent.width, .height = swapchain->extent.height, .layers = 1};
		VK_CHECK(vkCreateFramebuffer(core->device, &scene_info, NULL, &pass->framebuffers[i]), "Failed to create linear scene framebuffer");

		VkFramebufferCreateInfo present_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass->presentRenderPass, .attachmentCount = 1, .pAttachments = &swapchain->views[i], .width = swapchain->extent.width, .height = swapchain->extent.height, .layers = 1};
		VK_CHECK(vkCreateFramebuffer(core->device, &present_info, NULL, &pass->presentFramebuffers[i]), "Failed to create presentation framebuffer");
	}
}

void vulkan_render_pass_destroy(VulkanRenderPass *pass, VkDevice device)
{
	for (uint32_t i = 0; i < pass->imageCount; i++) {
		if (pass->framebuffers && pass->framebuffers[i] != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(device, pass->framebuffers[i], NULL);
		}
		if (pass->presentFramebuffers && pass->presentFramebuffers[i] != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(device, pass->presentFramebuffers[i], NULL);
		}
		if (pass->colorViews && pass->colorViews[i] != VK_NULL_HANDLE) {
			vkDestroyImageView(device, pass->colorViews[i], NULL);
		}
		if (pass->colorImages && pass->colorImages[i] != VK_NULL_HANDLE) {
			vkDestroyImage(device, pass->colorImages[i], NULL);
		}
		if (pass->colorMemories && pass->colorMemories[i] != VK_NULL_HANDLE) {
			vkFreeMemory(device, pass->colorMemories[i], NULL);
		}
		if (pass->depthViews && pass->depthViews[i] != VK_NULL_HANDLE) {
			vkDestroyImageView(device, pass->depthViews[i], NULL);
		}
		if (pass->depthImages && pass->depthImages[i] != VK_NULL_HANDLE) {
			vkDestroyImage(device, pass->depthImages[i], NULL);
		}
		if (pass->depthMemories && pass->depthMemories[i] != VK_NULL_HANDLE) {
			vkFreeMemory(device, pass->depthMemories[i], NULL);
		}
	}
	free(pass->framebuffers);
	free(pass->presentFramebuffers);
	free(pass->colorImages);
	free(pass->colorMemories);
	free(pass->colorViews);
	free(pass->depthImages);
	free(pass->depthMemories);
	free(pass->depthViews);
	pass->framebuffers = NULL;
	pass->presentFramebuffers = NULL;
	pass->colorImages = NULL;
	pass->colorMemories = NULL;
	pass->colorViews = NULL;
	pass->depthImages = NULL;
	pass->depthMemories = NULL;
	pass->depthViews = NULL;
	if (pass->presentRenderPass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(device, pass->presentRenderPass, NULL);
		pass->presentRenderPass = VK_NULL_HANDLE;
	}
	if (pass->renderPass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(device, pass->renderPass, NULL);
		pass->renderPass = VK_NULL_HANDLE;
	}
	pass->imageCount = 0;
}
