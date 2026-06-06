#include "vulkan/render_pass.h"

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "vulkan/utils.h"

void vulkan_render_pass_create(VulkanRenderPass *pass, VulkanCore *core, VulkanSwapchain *swapchain)
{
	pass->framebuffers = NULL;
	pass->renderPass = VK_NULL_HANDLE;

	VkAttachmentDescription colorAttachment = {.format = swapchain->imageFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

	VkAttachmentDescription depthAttachment = {.format = swapchain->depthFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkAttachmentReference colorAttachmentRef = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthAttachmentRef = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef, .pDepthStencilAttachment = &depthAttachmentRef};

	VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, .srcAccessMask = 0, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};

	VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
	VkRenderPassCreateInfo renderPassInfo = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = attachments, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};

	VK_CHECK(vkCreateRenderPass(core->device, &renderPassInfo, NULL, &pass->renderPass), "Failed to create render pass");

	pass->imageCount = swapchain->imageCount;
	pass->framebuffers = malloc(sizeof(VkFramebuffer) * swapchain->imageCount);
	for (uint32_t i = 0; i < swapchain->imageCount; i++) {
		VkImageView attachmentViews[] = {swapchain->views[i], swapchain->depthView};
		VkFramebufferCreateInfo framebufferInfo = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass->renderPass, .attachmentCount = 2, .pAttachments = attachmentViews, .width = swapchain->extent.width, .height = swapchain->extent.height, .layers = 1};
		VK_CHECK(vkCreateFramebuffer(core->device, &framebufferInfo, NULL, &pass->framebuffers[i]), "Failed to create framebuffer");
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
