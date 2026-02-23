#version 450

layout(push_constant) uniform Constants {
    float alpha;
} pc;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in float fragGlow;
layout(location = 4) in flat int fragDegree;
layout(location = 5) in float fragSelected;

layout(location = 0) out vec4 outColor;

void main() {
    // 1. SDF Math for N-gon based on Node Degree
    vec2 uv = fragTexCoord;
    int N = max(3, min(fragDegree, 12)); // Cap at 12 to maintain readable angularity
    
    float a = atan(uv.x, uv.y) + 3.1415926535;
    float r = 6.2831853071 / float(N);
    float d = cos(floor(0.5 + a / r) * r - a) * length(uv);
    
    // Cut the shape out of the tile!
    if (d > 0.6) {
        discard;
    }

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(fragNormal, lightDir), 0.2);

    vec3 baseColor = fragColor * diff;
    vec3 glowColor = fragColor * fragGlow * 1.5;
    
    // Circuit board border styling
    if (d > 0.5) {
        baseColor *= 0.5; // Darken the outer edge to frame the node
    }
    
    float finalAlpha = pc.alpha;
    if (fragSelected > 0.5) {
        finalAlpha = 1.0;
    }
    
    outColor = vec4(baseColor + glowColor, finalAlpha);
}