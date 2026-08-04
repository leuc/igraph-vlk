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
	float seq_time;
	float seq_stride;
	float seq_duration;
	float _reserved;
}
anim;

layout(std430, binding = 5) readonly buffer NodeAnimationStep
{
	int nodeStep[];
};

layout(std430, binding = 6) readonly buffer EdgeAnimationSource
{
	uint edgeSource[];
};

struct SPLCEdge
{
	uint target_node;
	float weight;
};

layout(std430, binding = 2) readonly buffer SPLCEdgeBuffer
{
	SPLCEdge splc_edges[];
};

layout(std430, binding = 3) readonly buffer MaxBuffer
{
	uint global_max_weight_uint;
};

layout(std430, binding = 7) readonly buffer EdgeAnimationValue
{
	float edgeValue[];
};

layout(std430, binding = 9) readonly buffer EdgeAnimationEventOffsets
{
	uint edgeEventOffsets[];
};

struct EdgeAnimationEvent
{
	float start_time;
	float duration;
	float value;
	float _reserved;
};

layout(std430, binding = 10) readonly buffer EdgeAnimationEvents
{
	EdgeAnimationEvent edgeEvents[];
};

layout(push_constant) uniform SPLCConstants
{
	layout(offset = 128) uint segmentsPerEdge;
}
splcPC;

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

void main()
{
	vec3 pos = mix(prevInPosition, inPosition, anim.transition_t);
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);

	float max_w = uintBitsToFloat(global_max_weight_uint);

	fragColor = inColor;
	uint edge_index = gl_VertexIndex / (splcPC.segmentsPerEdge * 2);
	const float FADE = 0.3;
	float reveal_t = anim.seq_duration > 0.0 ? smoothstep(0.0, FADE, anim.seq_time - float(nodeStep[edgeSource[edge_index]]) * anim.seq_stride) : 1.0;
	if (max_w > 0.0) {
		float w = splc_edges[edge_index].weight;
		float intensity = clamp(pow(log(w + 1.0) / log(max_w + 1.0), 0.3), 0.0, 1.0);
		fragColor = inColor * (0.2 + 0.8 * intensity) * reveal_t;
		fragAlpha = inAlpha * (0.12 + 0.88 * intensity);
	} else {
		vec3 base = mix(vec3(0.15), inColor, reveal_t);
		float intensity = clamp(pow(edgeValue[edge_index], 0.3), 0.0, 1.0);
		fragColor = base * (0.2 + 0.8 * intensity);
		fragAlpha = inAlpha;
	}

	float event_pulse = 0.0;
	for (uint i = edgeEventOffsets[edge_index]; i < edgeEventOffsets[edge_index + 1]; i++) {
		EdgeAnimationEvent event = edgeEvents[i];
		if (event.duration <= 0.0)
			continue;
		float progress = (anim.seq_time - event.start_time) / event.duration;
		float leading = smoothstep(inNormalizedPos - 0.12, inNormalizedPos, progress);
		float trailing = 1.0 - smoothstep(inNormalizedPos, inNormalizedPos + 0.18, progress);
		event_pulse = max(event_pulse, leading * trailing * clamp(event.value, 0.0, 1.0));
	}
	fragColor += inColor * event_pulse;
	fragSelected = inSelected;
	fragNormalizedPos = inNormalizedPos;
	fragVisible = inVisible;
	if (max_w <= 0.0)
		fragAlpha = inAlpha;
}
