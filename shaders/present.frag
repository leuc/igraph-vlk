#version 450

layout(binding = 0) uniform sampler2D sceneImage;

layout(push_constant) uniform PresentPushConstants
{
	uint outputMode;
	float referenceNits;
	float peakNits;
	float highlightNits;
}
outputState;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

vec3 linear_to_srgb(vec3 color)
{
	vec3 low = color * 12.92;
	vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - vec3(0.055);
	return mix(high, low, lessThanEqual(color, vec3(0.0031308)));
}

vec3 bt709_to_bt2020(vec3 color)
{
	return mat3(0.6274040, 0.0690970, 0.0163916, 0.3292820, 0.9195400, 0.0880132, 0.0433136, 0.0113612, 0.8955950) * color;
}

float map_hdr_relative(float value)
{
	if (value <= 1.0)
		return max(value, 0.0);
	float relativePeak = outputState.highlightNits / outputState.referenceNits;
	float t = clamp((value - 1.0) / 3.0, 0.0, 1.0);
	t = t * t * (3.0 - 2.0 * t);
	return 1.0 + (relativePeak - 1.0) * t;
}

float st2084_encode(float nits)
{
	const float m1 = 2610.0 / 16384.0;
	const float m2 = 2523.0 / 32.0;
	const float c1 = 3424.0 / 4096.0;
	const float c2 = 2413.0 / 128.0;
	const float c3 = 2392.0 / 128.0;
	float normalized = clamp(nits / 10000.0, 0.0, 1.0);
	float powered = pow(normalized, m1);
	return pow((c1 + c2 * powered) / (1.0 + c3 * powered), m2);
}

void main()
{
	vec3 scene = max(texture(sceneImage, fragUV).rgb, vec3(0.0));
	if (outputState.outputMode == 0u) {
		outColor = vec4(linear_to_srgb(clamp(scene, vec3(0.0), vec3(1.0))), 1.0);
		return;
	}
	vec3 mapped = vec3(map_hdr_relative(scene.r), map_hdr_relative(scene.g), map_hdr_relative(scene.b));
	vec3 bt2020 = max(bt709_to_bt2020(mapped), vec3(0.0));
	vec3 nits = min(bt2020 * outputState.referenceNits, vec3(outputState.peakNits));
	outColor = vec4(st2084_encode(nits.r), st2084_encode(nits.g), st2084_encode(nits.b), 1.0);
}
