#version 450

// ─────────────────────────────────────────────
//  voxel.vert
//
//  Transforms chunk vertices and prepares data
//  for Minecraft-style per-face directional
//  lighting + vertex-baked AO.
// ─────────────────────────────────────────────

// ── Vertex inputs (must match VoxelVertex) ───
layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec2  inUV;
layout(location = 2) in uint  inFaceIndex;  // 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
layout(location = 3) in uint  inAO;         // 0..3

// ── Push constants ────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4  model;
    float atlasRows;
    float atlasCols;
} pc;

// ── Descriptor set 0: global camera UBO ──────
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} camera;

// ── Outputs to fragment shader ───────────────
layout(location = 0) out vec2  fragUV;
layout(location = 1) out float fragLight;   // combined directional + AO

// ─────────────────────────────────────────────
//  Face brightness table (Minecraft values)
//
//  Mimics the original game's fixed directional
//  lighting without needing a light direction
//  uniform.  Top faces are brightest; sides are
//  slightly darker; bottom is darkest.
//
//  Index matches inFaceIndex:
//    0 = +X (east)   → 0.80
//    1 = -X (west)   → 0.80
//    2 = +Y (top)    → 1.00
//    3 = -Y (bottom) → 0.50
//    4 = +Z (south)  → 0.60
//    5 = -Z (north)  → 0.60
// ─────────────────────────────────────────────
const float FACE_BRIGHTNESS[6] = float[](
    0.80, 0.80, 1.00, 0.50, 0.60, 0.60
);

// AO curve — smooth out the [0,3] integer range
// into a pleasant darkening that matches the
// look of Minecraft's original AO implementation.
const float AO_CURVE[4] = float[](
    0.40,   // 0: fully occluded corner
    0.60,   // 1
    0.80,   // 2
    1.00    // 3: open corner, no occlusion
);

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);

    gl_Position = camera.proj * camera.view * worldPos;

    // Pass UV straight through — atlas tile selection
    // is done at mesh-build time by writing the correct
    // UV sub-region into each vertex.
    fragUV = inUV;

    // Combined light value: face direction × AO
    // Both factors are in [0,1]; product keeps the
    // AO darkening proportional to face brightness.
    uint faceIdx = min(inFaceIndex, 5u);
    uint aoIdx   = min(inAO,        3u);

    fragLight = FACE_BRIGHTNESS[faceIdx] * AO_CURVE[aoIdx];
}
