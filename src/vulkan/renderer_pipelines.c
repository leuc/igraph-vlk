#include "vulkan/renderer_pipelines.h"
#include "vulkan/pipeline_compute.h"
#include "vulkan/pipeline_graphics.h"
#include "vulkan/pipeline_ui.h"

int renderer_create_pipelines(Renderer *r)
{
	renderer_create_graphics_pipelines(r);
	renderer_create_ui_pipelines(r);
	renderer_create_compute_pipelines(r);
	return 0;
}
