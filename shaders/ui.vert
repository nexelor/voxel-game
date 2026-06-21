#version 450

// ─────────────────────────────────────────────
//  ui.vert
//
//  Converts 2D pixel-space coordinates to
//  Vulkan NDC.  Origin (0,0) = top-left window.
//  No view/projection matrix needed — just a
//  simple linear remap:
//
//    NDC.x = (px / screenW) * 2 - 1
//    NDC.y = (py / screenH) * 2 - 1   (Y already down in Vulkan)
// ─────────────────────────────────────────────

layout(location = 0) in vec2  inPosition;
layout(location = 1) in vec2  inUV;
layout(location = 2) in uint  inColor;     // packed RGBA8

layout(push_constant) uniform PC {
    float screenWidth;
    float screenHeight;
} pc;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec4  fragColor;

void main() {
    // Unpack RGBA8 → vec4
    fragColor = vec4(
        float( inColor        & 0xFFu) / 255.0,
        float((inColor >>  8) & 0xFFu) / 255.0,
        float((inColor >> 16) & 0xFFu) / 255.0,
        float((inColor >> 24) & 0xFFu) / 255.0
    );

    fragUV = inUV;

    vec2 ndc = (inPosition / vec2(pc.screenWidth, pc.screenHeight)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
