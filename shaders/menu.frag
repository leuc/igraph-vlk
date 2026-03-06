#version 450

layout(location = 0) out vec4 outColor;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in float fragTexId;
layout(location = 6) in vec3 fragWorldPos;
layout(location = 7) in float fragHovered;

void main()
{
	// Default: Menu Card Base (-1.0)
	vec4 baseColor = vec4(0.08, 0.1, 0.12, 0.95);

	if (fragTexId < -2.5) {
		// -3.0: Info Card Base (Distinct Teal/Slate)
		baseColor = vec4(0.06, 0.12, 0.16, 0.95);
	} else if (fragTexId < -1.5) {
		// -2.0: Title Bar Background (Lighter Grey/Blue)
		baseColor = vec4(0.18, 0.22, 0.28, 1.0);
	} else if (fragTexId > -0.5) {
		// >= 0.0: List Items
		baseColor = vec4(0.0, 0.0, 0.0, 0.0); // Transparent by default
		if (fragHovered > 0.5) {
			baseColor = vec4(0.3, 0.4, 0.5, 0.8); // Hover Highlight
		}
	}

	outColor = baseColor;
}
