#version 450

layout(binding = 4) uniform GlobalAnimState
{
	float time;
	float delta_time;
	uint frame_count;
	float transition_t;
	float seq_time;
	float seq_stride;
	float seq_duration;
	float _reserved;
}
anim;

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
		outColor = vec4(fragColor.rgb, alpha * fragColor.a);
	} else {
		outColor = fragColor;
	}
}
