#version 450

layout(push_constant) uniform Constants {
    float alpha;
} pc;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in float fragGlow;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(fragNormal, lightDir), 0.2);
    
    vec3 baseColor = fragColor * diff;
    vec3 glowColor = fragColor * fragGlow * 1.5; // Emissive boost
    
    outColor = vec4(baseColor + glowColor, pc.alpha);
}
