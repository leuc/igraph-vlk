#version 450

layout(binding = 1) uniform sampler2D texSampler;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    float alpha = texture(texSampler, fragTexCoord).r;
    if (alpha < 0.1) discard;
    outColor = vec4(1.0, 1.0, 1.0, alpha); // White labels
}
