#version 450

layout (binding = 0) uniform UniformBufferObject
{
  mat4 model;
  mat4 view;
  mat4 proj;
}
ubo;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in vec3 instancePos;
layout (location = 4) in vec3 instanceColor;
layout (location = 5) in float instanceSize;
layout (location = 6) in float instanceGlow;
layout (location = 7) in int instanceDegree;
layout (location = 8) in float instanceSelected;

layout (location = 0) out vec3 fragNormal;
layout (location = 1) out vec2 fragTexCoord;
layout (location = 2) out vec3 fragColor;
layout (location = 3) out float fragGlow;
layout (location = 4) out flat int fragDegree;
layout (location = 5) out float fragSelected;

void
main ()
{
  float finalSize = 0.5 * instanceSize;

  // Orient the tile flat on the surface of the layout sphere
  vec3 normal = normalize (instancePos);
  if (length (instancePos) < 0.001)
    normal = vec3 (0.0, 1.0, 0.0);

  vec3 upGuide = vec3 (0.0, 1.0, 0.0);
  if (abs (normal.y) > 0.999)
    upGuide = vec3 (1.0, 0.0, 0.0);

  vec3 rightVec = normalize (cross (upGuide, normal));
  vec3 upVec = cross (normal, rightVec);

  // Flatten geometry into a 2D tile mapped along the tangent plane
  vec3 flatPos = rightVec * inPosition.x + upVec * inPosition.y;
  vec3 worldPos = (flatPos * finalSize) + instancePos;

  gl_Position = ubo.proj * ubo.view * ubo.model * vec4 (worldPos, 1.0);

  fragNormal = normal; // Normal always faces exactly outward from the sphere
  fragTexCoord
      = inPosition
            .xy; // Pass the local flattened X/Y coordinates to the SDF cutter
  fragColor = instanceColor;
  fragGlow = instanceGlow;
  fragDegree = instanceDegree;
  fragSelected = instanceSelected;
}