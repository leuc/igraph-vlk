#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Per-character instance data
layout(location = 2) in vec2 screenPos;
layout(location = 3) in vec4 charRect;
layout(location = 4) in vec4 charUV;
layout(location = 5) in vec4 color;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float isText;

void main() {
	if (color.a < 0.0) {
		// Render background quad at the bottom
		// inPosition is likely a quad [0,1] or similar
		// Let's force it to bottom bar [-1, 1] in X, [0.9, 1.0] in Y
		float x = mix(-1.0, 1.0, inPosition.x);
		float y = mix(0.9, 1.0, inPosition.y);
		gl_Position = vec4(x, y, 0.0, 1.0);
		fragColor = vec4(0.0, 0.0, 0.0, 0.7);
		isText = 0.0;
	} else {
		// Render character
		// screenPos.y is -0.95 (bottom), but Vulkan Y is down.
		// Let's use 0.95 for bottom.
		float x = mix(charRect.x, charRect.z, inPosition.x);
		float y = mix(charRect.y, charRect.w, inPosition.y);

		vec2 pos = screenPos + vec2(x / 1720.0, y / 720.0);
		gl_Position = vec4(pos, 0.0, 1.0);

		fragTexCoord = vec2(mix(charUV.x, charUV.z, inTexCoord.x),
							mix(charUV.y, charUV.w, inTexCoord.y));
		fragColor = color;
		isText = 1.0;
	}
}
