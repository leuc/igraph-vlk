#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 instancePos;
layout(location = 4) in vec3 instanceColor;
layout(location = 5) in float instanceSize;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragColor;

void main() {
    // Scale node by instanceSize. Base scale is 0.5.
    float finalSize = 0.5 * instanceSize;
    vec3 worldPos = (inPosition * finalSize) + instancePos;
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;
    fragColor = instanceColor;
}
