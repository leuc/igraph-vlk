#version 450

layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in float fragTexId;
layout(location = 6) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Texture sampler for the icon atlas
layout(binding = 0) uniform sampler2D texSampler;

// Simple lighting for depth perception
layout(push_constant) uniform PushConstants {
    vec4 lightDir;      // World space light direction (normalized)
    vec4 viewPos;       // Camera position in world space
} pc;

void main() {
    // Sample the icon texture atlas
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Discard transparent fragments
    if (texColor.a < 0.1) discard;
    
    // Simple Lambertian shading for 3D effect
    // Compute normal from the quad's world position (approximate)
    // For a flat quad facing the camera, the normal is approximately view direction
    vec3 viewDir = normalize(pc.viewPos.xyz - fragWorldPos);
    vec3 normal = -viewDir; // Quad faces camera
    
    float diffuse = max(dot(normal, normalize(pc.lightDir.xyz)), 0.2);
    
    outColor = vec4(texColor.rgb * diffuse, texColor.a);
}
