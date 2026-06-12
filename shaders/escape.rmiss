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
	payload.escaped = 1;
}
