#version 450

layout(push_constant) uniform Constants
{
	float alpha;
}
pc;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in flat int fragDegree;
layout(location = 4) in float fragSelected;

layout(location = 0) out vec4 outColor;

void main()
{
	// 1. SDF Math for N-gon based on Node Degree
	vec2 uv = fragTexCoord;
	int N = max(3, min(fragDegree, 12)); // Cap at 12 to maintain readable angularity

	float a = atan(uv.x, uv.y) + 3.1415926535;
	float r = 6.2831853071 / float(N);
	float d = cos(floor(0.5 + a / r) * r - a) * length(uv);

	// Edge ring: bright highlight near SDF boundary
	float edge_width = 0.04;
	float edge_inner = 0.6 - edge_width;
	float edge_outer = 0.6 + edge_width;
	float edge_factor = smoothstep(edge_inner, 0.6, d) - smoothstep(0.6, edge_outer, d);

	// Cut the shape out (including edge ring)
	if (d > edge_outer) {
		discard;
	}

	vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
	float diff = max(dot(fragNormal, lightDir), 0.2);

	vec3 baseColor = fragColor * diff;

	// Edge ring color (brighter, slightly desaturated)
	vec3 edgeColor = mix(baseColor, vec3(1.0), 0.6);

	// Blend interior with edge ring
	vec3 finalColor = mix(baseColor, edgeColor * 1.5, edge_factor);

	// Circuit board border styling
	if (d > 0.5 && d <= 0.6) {
		finalColor *= 0.7;
	}

	float finalAlpha = 1.0;
	if (fragSelected > 0.5) {
		finalAlpha = 1.0;
		finalColor = mix(finalColor, vec3(1.0, 1.0, 0.0), 0.3);
	}

	outColor = vec4(finalColor, finalAlpha * pc.alpha);
}
