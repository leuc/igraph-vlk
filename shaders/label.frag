#version 450

layout(binding = 1) uniform sampler2D texSampler;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float inSelected;
layout(location = 0) out vec4 outColor;

void main()
{
	float a = texture(texSampler, fragTexCoord).r;
	if (inSelected > 1.5) {
		outColor = vec4(0.0, 0.0, 0.0, 1.0);
	} else if (inSelected > 0.5) {
		if (a < 0.1)
			discard;
		outColor = vec4(1.0, 1.0, 1.0, 1.0);
	} else {
		if (a < 0.1)
			discard;
		outColor = vec4(1.0, 1.0, 1.0, a);
	}
}
