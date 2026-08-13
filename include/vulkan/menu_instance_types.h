/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_MENU_INSTANCE_TYPES_H
#define VULKAN_MENU_INSTANCE_TYPES_H

#include <cglm/cglm.h>

typedef struct
{
	vec3 worldPos;
	vec2 texCoord;
	float texId;
	vec3 scale;
	vec4 rotation;
	float hovered;
} MenuInstance;

typedef struct
{
	vec3 worldPos;
	vec4 bgColor;
	vec3 scale;
	vec4 rotation;
	vec4 textUV;
	vec4 textRegion;
} TextQuadInstance;

#endif
