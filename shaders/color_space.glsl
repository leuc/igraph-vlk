layout(constant_id = 0) const bool SCENE_LINEAR_OUTPUT = true;

layout(std140, binding = 4) uniform ColorOutputState
{
	float time;
	float delta_time;
	uint frame_count;
	float transition_t;
	float playhead;
	float fade;
	uint reveal_mask;
	float hdr_highlights;
}
colorOutput;

vec3 srgb_to_linear(vec3 color)
{
	vec3 low = color / 12.92;
	vec3 high = pow((color + vec3(0.055)) / 1.055, vec3(2.4));
	return mix(high, low, lessThanEqual(color, vec3(0.04045)));
}

vec3 scene_output_color(vec3 color)
{
	return SCENE_LINEAR_OUTPUT ? srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))) : color;
}

vec3 scene_output_highlight(vec3 color)
{
	return SCENE_LINEAR_OUTPUT ? srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))) * mix(1.0, 4.0, colorOutput.hdr_highlights) : color;
}
