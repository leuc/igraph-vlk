#version 450

layout(location = 0) out vec4 outColor;
layout(location = 7) in float fragHovered;

// Menu Background: Modern Deep Dark Slate with subtle transparency
void main()
{
	// Dark grey-blue base (0.05, 0.07, 0.1) with 0.9 alpha
	vec4 baseColor = vec4(0.08, 0.1, 0.12, 0.9);

	// Highlight if hovered: slightly lighter and more opaque
	if (fragHovered > 0.5) {
		outColor = baseColor + vec4(0.1, 0.1, 0.15, 0.1);
	} else {
		outColor = baseColor;
	}
}
