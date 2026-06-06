#version 450

layout(push_constant) uniform PushConstants
{
	mat4 view;
	mat4 proj;
}
pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
	gl_Position = pc.proj * pc.view * vec4(inPos, 1.0);
	outColor = inColor;
}
