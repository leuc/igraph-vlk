#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragSelected;
layout(location = 2) in float fragAnimationProgress;
layout(location = 3) in flat int fragAnimationDirection;
layout(location = 4) in flat int fragIsAnimating;
layout(location = 5) in float fragNormalizedPos;

layout(location = 0) out vec4 outColor;

void main() {
	float alpha = 0.8;
	vec3 finalColor = fragColor;

	if (fragSelected > 0.5) {
		alpha = 1.0;
	}

	// "Subway" animation effect
	if (fragIsAnimating > 0) {
		float tube_segment_length = 0.15; // Length of the glowing segment
		float tube_glow_intensity = 2.5;  // How bright the tube segment is

		float start_segment_pos =
			fragAnimationProgress - tube_segment_length * 0.5;
		float end_segment_pos =
			fragAnimationProgress + tube_segment_length * 0.5;

		// Check if the current fragment position is within the animating tube
		// segment
		if (fragNormalizedPos >= start_segment_pos &&
			fragNormalizedPos <= end_segment_pos) {
			// Apply a glowing effect by ADDING the color rather than mixing
			// out-of-bounds
			finalColor =
				finalColor + (vec3(1.0, 1.0, 0.0) * tube_glow_intensity);
			alpha = 1.0;
		}
	}

	outColor = vec4(finalColor, alpha);
}
