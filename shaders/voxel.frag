#version 450

// ─────────────────────────────────────────────
//  voxel.frag
//
//  Samples the block texture atlas, applies the
//  directional light + AO factor from the vertex
//  stage, and adds simple exponential distance
//  fog matching Minecraft's aesthetic.
// ─────────────────────────────────────────────

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragLight;

layout(location = 0) out vec4 outColor;

// ── Descriptor set 0: global camera UBO ──────
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} camera;

// ── Descriptor set 1: block texture atlas ────
layout(set = 1, binding = 0) uniform sampler2D blockAtlas;

// ── Fog parameters (could become a UBO later) ─
// Fog starts fading at fogStart blocks away and
// is fully opaque at fogEnd.  Tweak to taste.
const float FOG_START   = 128.0;
const float FOG_END     = 192.0;
const vec3  FOG_COLOR   = vec3(0.63, 0.79, 1.00); // daytime sky blue

void main() {
    // Sample the texture atlas at the interpolated UV
    vec4 texColor = texture(blockAtlas, fragUV);

    // Discard fully transparent pixels (leaf blocks, glass alpha cutout)
    if (texColor.a < 0.05)
        discard;

    // Apply directional lighting + ambient occlusion
    vec3 litColor = texColor.rgb * fragLight;

    // ── Distance fog ─────────────────────────
    // gl_FragCoord.w = 1/w_clip = 1/cameraSpaceDepth in a standard proj.
    // Reconstruct linear depth from the inverse of the clip-space w.
    // This avoids passing a separate depth varying from the vertex stage.
    float depth    = 1.0 / gl_FragCoord.w;
    float fogFactor = clamp((FOG_END - depth) / (FOG_END - FOG_START), 0.0, 1.0);

    vec3 finalColor = mix(FOG_COLOR, litColor, fogFactor);

    outColor = vec4(finalColor, texColor.a);
}
