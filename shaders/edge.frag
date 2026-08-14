#version 450
#extension GL_GOOGLE_include_directive : require

#include "color_space.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragSelected;
layout(location = 2) in float fragNormalizedPos;
layout(location = 3) in float fragVisible;
layout(location = 4) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main()
{
	if (fragVisible < 0.5)
		discard;

	float alpha = fragAlpha;
	vec3 final_color = fragColor;
	if (fragSelected > 0.5) {
		final_color = min(final_color * 1.4 + vec3(0.1), vec3(1.0));
		alpha = 1.0;
	}
	vec3 encoded = fragSelected > 0.5 ? scene_output_highlight(final_color) : scene_output_color(final_color);
	outColor = vec4(encoded, alpha);
}
