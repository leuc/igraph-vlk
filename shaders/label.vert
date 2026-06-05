#version 450

layout(binding = 0) uniform UniformBufferObject
{
	mat4 model;
	mat4 view;
	mat4 proj;
}
ubo;

layout(push_constant) uniform LODConstants
{
	vec3 cameraPos;
	float lodThreshold;
}
pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 2) in vec3 nodePos;
layout(location = 3) in vec4 charRect;
layout(location = 4) in vec4 charUV;

layout(location = 5) in vec3 fixedRight;
layout(location = 6) in vec3 fixedUp;
layout(location = 7) in float inSelected;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float outSelected;

void main()
{
	float x = mix(charRect.x, charRect.z, inPosition.x);
	float y = mix(charRect.y, charRect.w, inPosition.y);

	vec3 pos = nodePos + (fixedRight * x) + (fixedUp * -y);

	if (inSelected < 0.5 && distance(pos, pc.cameraPos) > pc.lodThreshold) {
		gl_Position = vec4(0.0);
		return;
	}

	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);

	fragTexCoord = vec2(mix(charUV.x, charUV.z, inTexCoord.x), mix(charUV.y, charUV.w, inTexCoord.y));
	outSelected = inSelected;
}
