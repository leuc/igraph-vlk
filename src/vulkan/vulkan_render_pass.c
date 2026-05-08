#include "vulkan/vulkan_render_pass.h"

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "vulkan/utils.h"

void vulkan_render_pass_create(VulkanRenderPass *pass, VulkanCore *core, VulkanSwapchain *swap)
{
	pass->framebuffers = NULL;
	pass->renderPass = VK_NULL_HANDLE;

	VkAttachmentDescription colorAtt = {.format = swap->imageFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

	VkAttachmentDescription depthAtt = {.format = swap->depthFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkAttachmentReference colorAttRef = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthAttRef = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttRef, .pDepthStencilAttachment = &depthAttRef};

	VkSubpassDependency dep = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, .srcAccessMask = 0, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};

	VkAttachmentDescription atts[] = {colorAtt, depthAtt};
	VkRenderPassCreateInfo rpInfo = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = atts, .subpassCount = 1, .pSubpasses = &sub, .dependencyCount = 1, .pDependencies = &dep};

	VK_CHECK(vkCreateRenderPass(core->device, &rpInfo, NULL, &pass->renderPass), "Failed to create render pass");

	pass->imageCount = swap->imageCount;
	pass->framebuffers = malloc(sizeof(VkFramebuffer) * swap->imageCount);
	for (uint32_t i = 0; i < swap->imageCount; i++) {
		VkImageView attachments[] = {swap->views[i], swap->depthView};
		VkFramebufferCreateInfo fbInfo = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass->renderPass, .attachmentCount = 2, .pAttachments = attachments, .width = swap->extent.width, .height = swap->extent.height, .layers = 1};
		VK_CHECK(vkCreateFramebuffer(core->device, &fbInfo, NULL, &pass->framebuffers[i]), "Failed to create framebuffer");
	}
}

void vulkan_render_pass_destroy(VulkanRenderPass *pass, VkDevice device)
{
	if (pass->framebuffers) {
		for (uint32_t i = 0; i < pass->imageCount; i++) {
			if (pass->framebuffers[i] != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device, pass->framebuffers[i], NULL);
		}
		free(pass->framebuffers);
		pass->framebuffers = NULL;
	}
	if (pass->renderPass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(device, pass->renderPass, NULL);
		pass->renderPass = VK_NULL_HANDLE;
	}
}
