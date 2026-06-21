#version 450

// ─────────────────────────────────────────────
//  ui.frag
//
//  The font texture is R8_UNORM:
//    1.0 = opaque pixel (glyph or white tile)
//    0.0 = transparent
//
//  For solid quads the white tile UV is sampled,
//  so the fragment alpha comes entirely from the
//  vertex color's alpha channel.
//
//  For text the glyph alpha gates the vertex
//  color, giving clean anti-aliasing-free text.
// ─────────────────────────────────────────────

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec4  fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

void main() {
    // R channel = coverage (1 = draw, 0 = transparent)
    float coverage = texture(fontAtlas, fragUV).r;
    outColor = fragColor * vec4(1.0, 1.0, 1.0, coverage);

    // Discard fully transparent fragments to avoid z-fighting
    if (outColor.a < 0.01)
        discard;
}
