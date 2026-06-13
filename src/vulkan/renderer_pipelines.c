#include "vulkan/renderer_pipelines.h"
#include "vulkan/pipeline_compute.h"
#include "vulkan/pipeline_graphics.h"
#include "vulkan/pipeline_ui.h"
#include "vulkan/rt_base.h"
#include "vulkan/rt_layout.h"

void renderer_create_pipelines(Renderer *r)
{
	renderer_create_graphics_pipelines(r);
	renderer_create_ui_pipelines(r);
	renderer_create_compute_pipelines(r);
	r->rt_base = rt_base_create(&r->core);
	yhrt_init_pipelines(r);
}
