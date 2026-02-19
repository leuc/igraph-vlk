#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(fragNormal, lightDir), 0.2);
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // If text pixel is set (white), use it, otherwise use base color
    if (texColor.r > 0.5) {
        outColor = vec4(vec3(1.0, 1.0, 0.0) * diff, 1.0); // Yellow text
    } else {
        outColor = vec4(vec3(0.5, 0.5, 0.8) * diff, 1.0); // Bluish sphere
    }
}
