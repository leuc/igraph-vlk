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
	float _pad;
	float transition_t;
}
anim;

layout(std430, binding = 5) readonly buffer BFSOrder
{
	int bfsRank[];
};

layout(std430, binding = 6) readonly buffer EdgeFrom
{
	uint edgeFrom[];
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

layout(std430, binding = 7) readonly buffer EdgeVis
{
	float edgeFlow[];
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
	float reveal_t = smoothstep(0.0, 0.3, anim.time - float(bfsRank[edgeFrom[edge_index]]) * anim._pad);
	if (max_w > 0.0) {
		float w = splc_edges[edge_index].weight;
		float intensity = clamp(pow(log(w + 1.0) / log(max_w + 1.0), 0.3), 0.0, 1.0);
		fragColor = inColor * (0.2 + 0.8 * intensity) * reveal_t;
	} else {
		float max_flow = edgeFlow[0];
		vec3 base = mix(vec3(0.15), inColor, reveal_t);
		if (max_flow > 0.0) {
			float w = edgeFlow[edge_index + 1];
			float intensity = clamp(pow(log(w + 1.0) / log(max_flow + 1.0), 0.3), 0.0, 1.0);
			fragColor = base * (0.2 + 0.8 * intensity);
		} else {
			fragColor = base;
		}
	}
	fragSelected = inSelected;
	fragNormalizedPos = inNormalizedPos;
	fragVisible = inVisible;
	fragAlpha = inAlpha;
}
