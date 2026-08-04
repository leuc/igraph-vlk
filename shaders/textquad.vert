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

// Quad vertex attributes (unit quad, centered at origin)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Per-instance: TextQuadInstance
layout(location = 2) in vec3 worldPos;
layout(location = 3) in vec4 bgColor;
layout(location = 4) in vec3 scale;
layout(location = 5) in vec4 rotation;
layout(location = 6) in vec4 textUV;
layout(location = 7) in vec4 textRegion;

layout(location = 0) out vec2 fragQuadUV; // 0..1 from top-left of quad
layout(location = 1) out vec4 fragBgColor;
layout(location = 2) out vec4 fragTextUV;
layout(location = 3) out vec4 fragTextRegion;

vec4 quat_mul(vec4 q1, vec4 q2)
{
	return vec4(q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y, q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x, q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w, q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z);
}

vec3 rotate_by_quat(vec3 v, vec4 q)
{
	vec4 qv = vec4(v, 0.0);
	vec4 conj = vec4(-q.xyz, q.w);
	vec4 result = quat_mul(quat_mul(q, qv), conj);
	return result.xyz;
}

void main()
{
	vec3 scaledPos = inPosition * scale;

	if (length(rotation) > 0.0) {
		scaledPos = rotate_by_quat(scaledPos, rotation);
	}

	vec3 finalPos = worldPos + scaledPos;
	gl_Position = ubo.proj * ubo.view * vec4(finalPos, 1.0);

	// Pull the depth slightly toward the camera to prevent Z-fighting with MenuInstance quads
	gl_Position.z -= 0.001 * gl_Position.w;

	// Quad-local UV: (0,0) = top-left, (1,1) = bottom-right
	fragQuadUV = vec2(inPosition.x + 0.5, 0.5 - inPosition.y);
	fragBgColor = bgColor;
	fragTextUV = textUV;
	fragTextRegion = textRegion;
}
