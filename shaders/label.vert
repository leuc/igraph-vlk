#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition; // Quad [0,1]
layout(location = 1) in vec2 inTexCoord; // Quad [0,1]

// Per-character instance data
layout(location = 2) in vec3 nodePos;
layout(location = 3) in vec4 charRect; // x0, y0, x1, y1
layout(location = 4) in vec4 charUV;   // u0, v0, u1, v1

layout(location = 0) out vec2 fragTexCoord;

void main() {
    vec3 camRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 camUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    
    // Interpolate rect bounds based on [0,1] input position
    float x = mix(charRect.x, charRect.z, inPosition.x);
    float y = mix(charRect.y, charRect.w, inPosition.y);
    
    // Scale down the Inconsolata glyphs which are quite large in pixel space
    float scale = 0.01; 
    
    vec3 pos = nodePos + (camRight * x * scale) + (camUp * -y * scale);
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);
    
    // Interpolate UVs
    fragTexCoord = vec2(mix(charUV.x, charUV.z, inTexCoord.x), mix(charUV.y, charUV.w, inTexCoord.y));
}
