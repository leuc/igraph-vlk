#version 450
#extension GL_GOOGLE_include_directive : require

#include "color_space.glsl"

layout(location = 0) in vec3 fragSdrColor;
layout(location = 1) in vec3 fragHdrColor;
layout(location = 2) in float fragSelected;
layout(location = 3) in float fragNormalizedPos;
layout(location = 4) in float fragVisible;
layout(location = 5) in float fragAlpha;
layout(location = 6) in float fragIntensity;
layout(location = 7) in float fragReveal;

layout(location = 0) out vec4 outColor;

void main()
{
	if (fragVisible < 0.5)
		discard;

	float alpha = fragAlpha;
	vec3 shaded = graph_scene_color(fragSdrColor, fragHdrColor) * mix(0.2, 1.0, fragIntensity);
	vec3 final_color = mix(scene_output_color(vec3(0.15)), shaded, fragReveal);
	if (fragSelected > 0.5) {
		final_color = min(final_color * 1.4 + scene_output_color(vec3(0.1)), vec3(1.0));
		alpha = 1.0;
	}
	vec3 encoded = fragSelected > 0.5 ? graph_scene_highlight(final_color) : final_color;
	outColor = vec4(encoded, alpha);
}
