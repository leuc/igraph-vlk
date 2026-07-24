#version 450

layout(binding = 4) uniform GlobalAnimState
{
	float time;
	float delta_time;
	uint frame_count;
	float _pad;
	float transition_t;
}
anim;

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
	vec3 finalColor = fragColor;

	if (fragSelected > 0.5) {
		alpha = 1.0;
	}
	outColor = vec4(finalColor, alpha);
}
