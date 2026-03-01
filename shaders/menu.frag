#version 450

layout(location = 0) out vec4 outColor;

// Menu Background: Modern Deep Dark Slate with subtle transparency
void main() {
    // Dark grey-blue base (0.05, 0.07, 0.1) with 0.9 alpha
    outColor = vec4(0.08, 0.1, 0.12, 0.9); 
}
