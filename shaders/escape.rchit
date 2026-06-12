#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
	uint hitCount;
	uint originNodeId;
	uint escaped;
	float lastHitT;
};
layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
	// No-op: the counting any-hit shader ignores all intersections
	// and the miss shader fires when the ray reaches open space.
	// Closest-hit is required by the pipeline but never executes.
}
