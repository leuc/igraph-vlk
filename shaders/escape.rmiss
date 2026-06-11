#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
	float hitDistance;
	uint originNodeId;
};
layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
	payload.hitDistance = gl_RayTmaxEXT;
}
