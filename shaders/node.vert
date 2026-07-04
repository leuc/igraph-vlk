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
}
anim;

layout(std430, binding = 5) readonly buffer BFSOrder {
	int bfsRank[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 instancePos;
layout(location = 2) in vec3 instanceColor;
layout(location = 3) in float instanceSize;
layout(location = 4) in int instanceDegree;
layout(location = 5) in float instanceSelected;
layout(location = 6) in float instanceVisible;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out flat int fragDegree;
layout(location = 3) out float fragSelected;
layout(location = 4) out float fragVisible;
layout(location = 5) out flat int fragBfsRank;

void main()
{
	float finalSize = 0.5 * instanceSize;

	// Orient the tile flat on the surface of the layout sphere
	vec3 normal = normalize(instancePos);
	if (length(instancePos) < 0.001)
		normal = vec3(0.0, 1.0, 0.0);

	vec3 upGuide = vec3(0.0, 1.0, 0.0);
	if (abs(normal.y) > 0.999)
		upGuide = vec3(1.0, 0.0, 0.0);

	vec3 rightVec = normalize(cross(upGuide, normal));
	vec3 upVec = cross(normal, rightVec);

	// Flatten geometry into a 2D tile mapped along the tangent plane
	vec3 flatPos = rightVec * inPosition.x + upVec * inPosition.y;
	vec3 worldPos = (flatPos * finalSize) + instancePos;

	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);

	fragTexCoord = inPosition.xy; // Pass the local flattened X/Y coordinates to the SDF cutter
	fragColor = instanceColor;
	fragDegree = instanceDegree;
	fragSelected = instanceSelected;
	fragVisible = instanceVisible;
	fragBfsRank = bfsRank[gl_InstanceIndex];
}