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

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main()
{
	outColor = inColor;
}
