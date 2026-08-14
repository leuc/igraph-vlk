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

vec3 linear_bt709_to_bt2020(vec3 color)
{
	return mat3(0.6274040, 0.0690970, 0.0163916, 0.3292820, 0.9195400, 0.0880132, 0.0433136, 0.0113612, 0.8955950) * color;
}

vec3 scene_output_color(vec3 color)
{
	return SCENE_LINEAR_OUTPUT ? linear_bt709_to_bt2020(srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0)))) : color;
}

vec3 graph_scene_color(vec3 sdr_srgb, vec3 hdr_linear_bt2020)
{
	if (!SCENE_LINEAR_OUTPUT)
		return sdr_srgb;
	return colorOutput.hdr_highlights > 0.5 ? hdr_linear_bt2020 : scene_output_color(sdr_srgb);
}

vec3 graph_scene_highlight(vec3 color)
{
	return SCENE_LINEAR_OUTPUT ? color * mix(1.0, 4.0, colorOutput.hdr_highlights) : color;
}
