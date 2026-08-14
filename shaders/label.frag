#version 450
#extension GL_GOOGLE_include_directive : require

#include "color_space.glsl"

layout(binding = 1) uniform sampler2D textAtlas;

layout(location = 0) in vec2 fragQuadUV;
layout(location = 1) in vec4 fragBgColor;
layout(location = 2) in vec4 fragTextUV;
layout(location = 3) in vec4 fragTextRegion;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 color = fragBgColor;

	if (fragTextUV.z > fragTextUV.x) {
		vec2 rMin = fragTextRegion.xy;
		vec2 rMax = fragTextRegion.zw;
		if (fragQuadUV.x >= rMin.x && fragQuadUV.x <= rMax.x && fragQuadUV.y >= rMin.y && fragQuadUV.y <= rMax.y) {
			vec2 localUV = (fragQuadUV - rMin) / (rMax - rMin);
			vec2 atlasUV = vec2(mix(fragTextUV.x, fragTextUV.z, localUV.x), mix(fragTextUV.y, fragTextUV.w, localUV.y));

			float a = textureLod(textAtlas, atlasUV, 0.0).r;

			if (a > 0.1) {
				color.rgb = mix(color.rgb, vec3(1.0), a);
				color.a = max(color.a, a);
			}
		}
	}

	if (color.a < 0.01)
		discard;
	outColor = vec4(scene_output_color(color.rgb), color.a);
}
