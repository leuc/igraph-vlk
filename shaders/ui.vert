#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 proj;
    mat4 view;
    vec3 cameraPos;
    vec3 cameraRight;
    vec3 cameraUp;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 2) in vec3 instancePosition;
layout(location = 3) in float instanceScale;
layout(location = 4) in int instanceIconIndex;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out int fragIconIndex;

void main() {
    vec3 vertexPosition = instancePosition + (ubo.cameraRight * inPosition.x + ubo.cameraUp * inPosition.y) * instanceScale;
    gl_Position = ubo.proj * ubo.view * vec4(vertexPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragIconIndex = instanceIconIndex;
}
