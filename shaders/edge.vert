#version 450

layout(binding = 0) uniform UniformBufferObject
{
	mat4 model;
	mat4 view;
	mat4 proj;
}
ubo;

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

layout(push_constant) uniform SPLCConstants
{
	layout(offset = 128) uint segmentsPerEdge;
}
splcPC;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inSelected;
layout(location = 3) in float inNormalizedPos;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragSelected;
layout(location = 2) out float fragNormalizedPos;

void main()
{
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);

	float max_w = uintBitsToFloat(global_max_weight_uint);

	fragColor = inColor;
	if (max_w > 0.0) {
		uint edge_index = gl_VertexIndex / (splcPC.segmentsPerEdge * 2);
		float w = splc_edges[edge_index].weight;
		float intensity = clamp(pow(log(w + 1.0) / log(max_w + 1.0), 0.3), 0.0, 1.0);
		fragColor = inColor * (0.2 + 0.8 * intensity);
	}
	fragSelected = inSelected;
	fragNormalizedPos = inNormalizedPos;
}
