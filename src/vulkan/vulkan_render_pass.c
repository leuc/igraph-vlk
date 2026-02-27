#include "vulkan/vulkan_render_pass.h"

#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "vulkan/utils.h"

void vulkan_render_pass_create(VulkanRenderPass *pass, VulkanCore *core,
								VulkanSwapchain *swap) {
	pass->framebuffers = NULL;
	pass->renderPass = VK_NULL_HANDLE;

	// Color attachment
	VkAttachmentDescription colorAttachment = {
		.format = swap->imageFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	// Subpass
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef};

	// Render pass dependencies
	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};

	// Create render pass
	VkRenderPassCreateInfo renderPassInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		NULL,
		0,
		1,
		&colorAttachment,
		1,
		&subpass,
		1,
		&dependency};

	VK_CHECK(vkCreateRenderPass(core->device, &renderPassInfo, NULL, &pass->renderPass),
			 "Failed to create render pass");

	// Create framebuffers
	pass->framebuffers = malloc(sizeof(VkFramebuffer) * swap->imageCount);
	for (uint32_t i = 0; i < swap->imageCount; i++) {
		VkImageView attachments[] = {swap->views[i]};

		VkFramebufferCreateInfo framebufferInfo = {
			VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			NULL,
			0,
			pass->renderPass,
			1,
			attachments,
			swap->extent.width,
			swap->extent.height,
			1};

		VK_CHECK(vkCreateFramebuffer(core->device, &framebufferInfo, NULL,
									 &pass->framebuffers[i]),
				 "Failed to create framebuffer");
	}
}

void vulkan_render_pass_destroy(VulkanRenderPass *pass, VkDevice device) {
	if (pass->framebuffers) {
		for (uint32_t i = 0; pass->framebuffers[i] != VK_NULL_HANDLE; i++) {
			vkDestroyFramebuffer(device, pass->framebuffers[i], NULL);
		}
		free(pass->framebuffers);
	}
	if (pass->renderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(device, pass->renderPass, NULL);
}
