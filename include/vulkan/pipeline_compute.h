/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef PIPELINE_COMPUTE_H
#define PIPELINE_COMPUTE_H

#include "vulkan/vulkan_types.h"

void renderer_create_compute_pipelines(Renderer *r);
void renderer_create_splc_compute_pipeline(Renderer *r);

#endif // PIPELINE_COMPUTE_H
