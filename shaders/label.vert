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

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 2) in vec3 worldPos;
layout(location = 3) in vec4 bgColor;
layout(location = 4) in vec3 scale;
layout(location = 5) in vec3 right;
layout(location = 6) in vec3 up;
layout(location = 7) in vec4 textUV;
layout(location = 8) in vec4 textRegion;

layout(location = 0) out vec2 fragQuadUV;
layout(location = 1) out vec4 fragBgColor;
layout(location = 2) out vec4 fragTextUV;
layout(location = 3) out vec4 fragTextRegion;

void main()
{
	float x = inPosition.x * scale.x;
	float y = inPosition.y * scale.y;

	vec3 pos = worldPos + right * x + up * y;

	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);

	gl_Position.z -= 0.001 * gl_Position.w;

	fragQuadUV = vec2(inPosition.x + 0.5, 0.5 - inPosition.y);
	fragBgColor = bgColor;
	fragTextUV = textUV;
	fragTextRegion = textRegion;
}
