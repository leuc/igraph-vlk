#version 450

layout(push_constant) uniform PushConstants {
    float alpha;
} pc;

layout(location = 0) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 0.8, 1.0));
    float diff = max(dot(normalize(fragNormal), lightDir), 0.2);
    vec3 baseColor = vec3(0.8, 0.9, 1.0);
    outColor = vec4(baseColor * diff + vec3(0.1), pc.alpha);
}
