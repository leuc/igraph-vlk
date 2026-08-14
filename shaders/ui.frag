#version 450
#extension GL_GOOGLE_include_directive : require

#include "color_space.glsl"

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float isText;

layout(location = 0) out vec4 outColor;

void main()
{
	if (isText > 0.5) {
		float alpha = texture(texSampler, fragTexCoord).r;
		if (alpha < 0.1)
			discard;
		outColor = vec4(scene_output_color(fragColor.rgb), alpha * fragColor.a);
	} else {
		outColor = vec4(scene_output_color(fragColor.rgb), fragColor.a);
	}
}
