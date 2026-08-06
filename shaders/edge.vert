#version 450

layout(binding = 0) uniform UniformBufferObject
{
	mat4 model;
	mat4 view;
	mat4 proj;
}
ubo;

layout(binding = 4) uniform GlobalAnimState
{
	float time;
	float delta_time;
	uint frame_count;
	float transition_t;
	float playhead;
	float fade;
	uint reveal_mask;
	float _reserved;
}
anim;

struct EdgeAnim
{
	float reveal_at;
	float strength;
};

layout(std430, binding = 2) readonly buffer EdgeAnimation
{
	uint strength_max_bits;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	EdgeAnim edges[];
}
edge_anim;

layout(push_constant) uniform EdgePushConstants
{
	layout(offset = 128) uint segmentsPerEdge;
}
edgePC;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inSelected;
layout(location = 3) in float inNormalizedPos;
layout(location = 4) in float inVisible;
layout(location = 5) in vec3 prevInPosition;
layout(location = 6) in float inAlpha;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragSelected;
layout(location = 2) out float fragNormalizedPos;
layout(location = 3) out float fragVisible;
layout(location = 4) out float fragAlpha;

float reveal_value(float reveal_at)
{
	return (anim.reveal_mask & 2u) != 0u ? smoothstep(0.0, max(anim.fade, 0.001), anim.playhead - reveal_at) : 1.0;
}

void main()
{
	vec3 pos = mix(prevInPosition, inPosition, anim.transition_t);
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);

	uint edge_index = gl_VertexIndex / (edgePC.segmentsPerEdge * 2);
	float max_strength = uintBitsToFloat(edge_anim.strength_max_bits);
	float intensity = max_strength > 0.0 ? clamp(edge_anim.edges[edge_index].strength / max_strength, 0.0, 1.0) : 0.0;
	intensity = pow(intensity, 0.3);
	float reveal = reveal_value(edge_anim.edges[edge_index].reveal_at);
	vec3 shaded = inColor * mix(0.2, 1.0, intensity);
	fragColor = mix(vec3(0.15), shaded, reveal);
	fragAlpha = inAlpha * mix(0.12, 1.0, intensity);
	fragSelected = inSelected;
	fragNormalizedPos = inNormalizedPos;
	fragVisible = inVisible;
}
