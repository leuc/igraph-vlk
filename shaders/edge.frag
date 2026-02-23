#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragSelected;
layout(location = 0) out vec4 outColor;

void main() {
    float alpha = 0.8;
    if (fragSelected > 0.5) {
        alpha = 1.0;
    }
    outColor = vec4(fragColor, alpha); // Semi-transparent edges
}
