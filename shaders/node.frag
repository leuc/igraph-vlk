#version 450

layout(binding = 4) uniform GlobalAnimState
{
	float time;
	float delta_time;
	uint frame_count;
	float _pad;
}
anim;

layout(std430, binding = 5) readonly buffer BFSOrder {
	int bfsRank[];
};

layout(push_constant) uniform Constants
{
	float alpha;
}
pc;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in flat int fragDegree;
layout(location = 3) in float fragSelected;
layout(location = 4) in float fragVisible;
layout(location = 5) in flat int fragBfsRank;

layout(location = 0) out vec4 outColor;

void main()
{
	if (fragVisible < 0.5)
		discard;

	vec3 c = mix(vec3(0.15), fragColor, smoothstep(0.0, 0.3, anim.time - float(fragBfsRank) * anim._pad));

	vec2 uv = fragTexCoord;
	float dist = length(uv);
	int deg = fragDegree;

	// Degree 0: dot
	if (deg == 0) {
		if (dist > 0.15)
			discard;
		outColor = vec4(c, 1.0);
		return;
	}

	// Degree 1: circle
	if (deg == 1) {
		if (dist > 1.0)
			discard;
		outColor = vec4(c, 1.0);
		return;
	}

	// Degree 2: circle with two halves of slightly different tones
	if (deg == 2) {
		if (dist > 1.0)
			discard;
		vec3 hc = c;
		if (uv.x > 0.0)
			hc = c * 0.7;
		outColor = vec4(hc, 1.0);
		return;
	}

	// Degree 3+: N-gon cutout
	float a = atan(uv.x, uv.y) + 3.1415926535;
	float r = 6.2831853071 / float(deg);
	float sector = floor(a / r);
	float d = cos(3.1415926535 / float(deg)) - dist * cos(a - (sector + 0.5) * r);

	if (d < 0.0)
		discard;

	outColor = vec4(c, 1.0);
}
