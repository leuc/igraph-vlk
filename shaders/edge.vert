#version 450

layout(binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 proj;
}
ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inSize;
layout(location = 3) in float inSelected;
layout(location = 4) in float inAnimationProgress;
layout(location = 5) in int inAnimationDirection;
layout(location = 6) in int inIsAnimating;
layout(location = 7) in float inNormalizedPos;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragSelected;
layout(location = 2) out float fragAnimationProgress;
layout(location = 3) out flat int fragAnimationDirection;
layout(location = 4) out flat int fragIsAnimating;
layout(location = 5) out float fragNormalizedPos;

void main() {
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
	// Use inSize to dim/brighten the edge
	fragColor = inColor * (0.2 + 0.8 * inSize);
	fragSelected = inSelected;
	fragAnimationProgress = inAnimationProgress;
	fragAnimationDirection = inAnimationDirection;
	fragIsAnimating = inIsAnimating;
	fragNormalizedPos = inNormalizedPos;
}
