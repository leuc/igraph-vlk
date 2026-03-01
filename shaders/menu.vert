#version 450

// Quad vertex attributes
layout(location = 0) in vec3 inPosition;   // Quad vertex position (local space, e.g., [-0.5, 0.5])
layout(location = 1) in vec2 inTexCoord;   // Quad texture coordinates [0,1]

// Per-instance data for each menu node
layout(location = 2) in vec3 worldPos;     // Calculated Cartesian world position (from spherical)
layout(location = 3) in vec2 texCoord;     // Icon texture region (sprite sheet) base UV
layout(location = 4) in float texId;       // Texture ID / icon index
layout(location = 5) in vec3 scale;        // Icon quad scaling
layout(location = 6) in vec4 rotation;    // Quaternion rotation (w, x, y, z)

// Uniform buffers
layout(std140, set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 4) out vec2 fragTexCoord;
layout(location = 5) out float fragTexId;
layout(location = 6) out vec3 fragWorldPos;

// Quaternion multiplication: q1 * q2
vec4 quat_mul(vec4 q1, vec4 q2) {
    return vec4(
        q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w,
        q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z
    );
}

// Rotate vector by quaternion
vec3 rotate_by_quat(vec3 v, vec4 q) {
    vec4 qv = vec4(v, 0.0);
    vec4 conj = vec4(-q.xyz, q.w);
    vec4 result = quat_mul(quat_mul(q, qv), conj);
    return result.xyz;
}

void main() {
    // Apply scale to the local quad vertex
    vec3 scaledPos = inPosition * scale;
    
    // Apply rotation if provided (default identity quaternion = (0,0,0,1))
    if (length(rotation) > 0.0) {
        scaledPos = rotate_by_quat(scaledPos, rotation);
    }
    
    // Apply instance world position (already in world Cartesian coordinates)
    vec3 finalPos = worldPos + scaledPos;
    
    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * vec4(finalPos, 1.0);
    
    // Pass-through texture coordinates (will be adjusted by sprite sheet)
    fragTexCoord = inTexCoord;
    fragTexId = texId;
    fragWorldPos = finalPos;
}
