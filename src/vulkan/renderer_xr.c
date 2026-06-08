#include "vulkan/renderer_xr.h"

#include "vulkan/images.h"
#include "vulkan/utils.h"

#ifdef USE_OPENXR
void renderer_setup_xr(Renderer *r, XrContext *xr)
{
	r->xr_view_count = xr->view_count;
	r->xrFormat = xr->swapchain_format;
	r->xrFramebuffers = malloc(sizeof(VkFramebuffer *) * xr->view_count);
	r->xrFramebufferImageCount = malloc(sizeof(uint32_t) * xr->view_count);

	VkRenderPass xrRenderPass = r->renderPass.renderPass;
	if (xr->swapchain_format != r->swapchain.imageFormat) {
		VkAttachmentDescription cAttXR = {.format = xr->swapchain_format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentDescription dAttXR = {.format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		VkAttachmentReference cAttRefXR = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentReference dAttRefXR = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		VkSubpassDescription subXR = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &cAttRefXR, .pDepthStencilAttachment = &dAttRefXR};
		VkAttachmentDescription attsXR[] = {cAttXR, dAttXR};
		VkRenderPassCreateInfo rpInfoXR = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = attsXR, .subpassCount = 1, .pSubpasses = &subXR};
		VK_CHECK(vkCreateRenderPass(r->core.device, &rpInfoXR, NULL, &r->renderPassXR), "Failed to create XR render pass");
		xrRenderPass = r->renderPassXR;
	}

	r->xrDepthImages = malloc(sizeof(VkImage) * xr->view_count);
	r->xrDepthImageMemories = malloc(sizeof(VkDeviceMemory) * xr->view_count);
	r->xrDepthImageViews = malloc(sizeof(VkImageView) * xr->view_count);

	for (uint32_t i = 0; i < xr->view_count; i++) {
		r->xrFramebufferImageCount[i] = xr->swapchains[i].image_count;
		r->xrFramebuffers[i] = malloc(sizeof(VkFramebuffer) * xr->swapchains[i].image_count);
		xr->swapchains[i].image_views = malloc(sizeof(VkImageView) * xr->swapchains[i].image_count);

		create_image(r->core.device, r->core.physicalDevice, xr->swapchains[i].width, xr->swapchains[i].height, r->swapchain.depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->xrDepthImages[i], &r->xrDepthImageMemories[i]);
		VK_CHECK(vkCreateImageView(r->core.device, &VK_IMAGE_VIEW_2D(r->xrDepthImages[i], r->swapchain.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT), NULL, &r->xrDepthImageViews[i]), "Failed to create XR depth image view");
		transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->xrDepthImages[i], r->swapchain.depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		for (uint32_t j = 0; j < xr->swapchains[i].image_count; j++) {
			VK_CHECK(vkCreateImageView(r->core.device, &VK_IMAGE_VIEW_2D(xr->swapchains[i].images[j], xr->swapchain_format, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &xr->swapchains[i].image_views[j]), "Failed to create XR swapchain image view");
			VkImageView attachments[] = {xr->swapchains[i].image_views[j], r->xrDepthImageViews[i]};
			VK_CHECK(vkCreateFramebuffer(r->core.device, &VK_FRAMEBUFFER_INFO(xrRenderPass, attachments, xr->swapchains[i].width, xr->swapchains[i].height), NULL, &r->xrFramebuffers[i][j]), "Failed to create XR framebuffer");
		}
	}
}
#endif
