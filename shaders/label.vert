#version 450

layout(binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 proj;
}
ubo;

layout(location = 0) in vec3 inPosition; // Quad [0,1]
layout(location = 1) in vec2 inTexCoord; // Quad [0,1]

// Per-character instance data
layout(location = 2) in vec3 nodePos;
layout(location = 3) in vec4 charRect; // x0, y0, x1, scale (.w)
layout(location = 4) in vec4 charUV;   // u0, v0, u1, v1

// Fixed orientation vectors from CPU
layout(location = 5) in vec3 fixedRight;
layout(location = 6) in vec3 fixedUp;

layout(location = 0) out vec2 fragTexCoord;

void main() {
	vec3 camRight = fixedRight;
	vec3 camUp = fixedUp;

	// Use charRect.w as the dynamic scale passed from CPU
	float scale = charRect.w;

	// Interpolate local quad coordinates [0,1] into glyph bounds
    // Note: charRect.z is x1, but we don't have y1 here if we use .w for scale.
    // However, stbtt_bakedchar yoff is relative to baseline. 
    // We can use the original logic if we pass y1 somewhere else, 
    // but charRect.y is already y0, so let's assume height is proportional or use scale.
    // Actually, renderer_geometry.c was setting charRect[1] = ci->y0 and charRect[3] was scale.
    // Wait, renderer_geometry.c has:
    // li[k].charRect[1] = ci->y0;
    // li[k].charRect[3] = 0.01f; // scale
    // It lost y1! Let's rethink. 
    
    // Better: use charRect.x,y for x0,y0 and charRect.z,w for x1,y1 
    // and pass scale in another attribute or as a constant if they are all similar.
    // But menu text and node labels have different scales (0.003 vs 0.01).
    
    // Let's use charRect for x0,y0,x1,y1 and use a push constant or another attribute for scale.
    // Or just use charRect.x,y,z as x0,y0,x1 and a fixed aspect ratio or similar.
    
    // Re-evaluating: renderer_geometry.c previously had:
    // li[k].charRect[0] = xoff + ci->x0;
    // li[k].charRect[1] = ci->y0;
    // li[k].charRect[2] = xoff + ci->x1;
    // li[k].charRect[3] = ci->y1;
    
    // Let's go back to that and use the instance's 'right' and 'up' vectors' MAGNITUDE for scale? 
    // No, that's messy.
    
    // Let's use charRect[3] for scale, and we can calculate height from scale if we assume font is square? No.
    
    // Let's use a PUSH CONSTANT for scale. 
    // But we draw all labels in one call.
    
    // Okay, I will use:
    // charRect.x = x0, charRect.y = y0, charRect.z = x1, charRect.w = y1
    // And I will pass scale in fixedRight.x (since it's a unit vector, we can scale it!)
    
    float x = mix(charRect.x, charRect.z, inPosition.x);
    float y = mix(charRect.y, charRect.w, inPosition.y);

    // If we scale the right/up vectors on CPU, we don't need a separate scale variable!
	vec3 pos = nodePos + (camRight * x) + (camUp * -y);
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);

	fragTexCoord = vec2(mix(charUV.x, charUV.z, inTexCoord.x),
						mix(charUV.y, charUV.w, inTexCoord.y));
}
