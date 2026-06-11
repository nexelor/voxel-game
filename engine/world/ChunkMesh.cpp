#include "ChunkMesh.hpp"
 
#include <glm/glm.hpp>
 
// ─────────────────────────────────────────────
//  Face definitions
//
//  Each face has:
//    - a faceIndex (matches the shader's FACE_BRIGHTNESS table)
//    - a normal direction (for AO neighbour lookups)
//    - 4 corner offsets relative to the block's (x,y,z) origin
//
//  Winding is counter-clockwise when seen from outside the block,
//  matching VK_FRONT_FACE_COUNTER_CLOCKWISE + back-face culling.
//
//  Corner layout (consistent across all faces):
//      0───1
//      │ ╲ │   tri0 = 0,1,2   tri1 = 0,2,3
//      3───2
// ─────────────────────────────────────────────
 
struct FaceDef {
    uint32_t  faceIndex;  // 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    glm::ivec3 normal;
    glm::vec3  corners[4];
    glm::vec2  uvs[4];    // local [0,1] UVs for the quad
};

// +X face (east, normal = +X)
// +Y face (top,  normal = +Y)
// etc.
static const FaceDef FACES[6] = {
    // +X
    { 0, { 1, 0, 0},
      {{ 1,1,0 }, { 1,1,1 }, { 1,0,1 }, { 1,0,0 }},
      {{ 0,0 },   { 1,0 },   { 1,1 },   { 0,1 }}
    },
    // -X
    { 1, {-1, 0, 0},
      {{ 0,1,1 }, { 0,1,0 }, { 0,0,0 }, { 0,0,1 }},
      {{ 0,0 },   { 1,0 },   { 1,1 },   { 0,1 }}
    },
    // +Y (top)
    { 2, { 0, 1, 0},
      {{ 0,1,1 }, { 1,1,1 }, { 1,1,0 }, { 0,1,0 }},
      {{ 0,0 },   { 1,0 },   { 1,1 },   { 0,1 }}
    },
    // -Y (bottom)
    { 3, { 0,-1, 0},
      {{ 0,0,0 }, { 1,0,0 }, { 1,0,1 }, { 0,0,1 }},
      {{ 0,0 },   { 1,0 },   { 1,1 },   { 0,1 }}
    },
    // +Z (south)
    { 4, { 0, 0, 1},
      {{ 0,1,1 }, { 0,0,1 }, { 1,0,1 }, { 1,1,1 }},  // flipped winding
      {{ 0,0 },   { 0,1 },   { 1,1 },   { 1,0 }}
    },
    // -Z (north)
    { 5, { 0, 0,-1},
      {{ 1,1,0 }, { 1,0,0 }, { 0,0,0 }, { 0,1,0 }},  // flipped winding
      {{ 0,0 },   { 0,1 },   { 1,1 },   { 1,0 }}
    },
};

// ─────────────────────────────────────────────
//  Vertex AO
//
//  Standard Minecraft-style per-vertex AO.
//  For a given face vertex we sample 3 neighbours
//  (side1, side2, corner) in the block grid.
//  Returns 0 (darkest) .. 3 (brightest).
// ─────────────────────────────────────────────
static uint32_t VertexAO(
    const Chunk& chunk,
    glm::ivec3 blockPos,   // the block being meshed
    glm::ivec3 normal,     // face normal
    glm::vec3 cornerOffset) // one of the 4 face corners (0 or 1 per axis)
{
    // The vertex sits at blockPos + cornerOffset.
    // We need the two axis-aligned side neighbours and the diagonal corner.
    // Which axes to sample depends on the face normal.
 
    // Build two tangent vectors (axes that are not the normal)
    glm::ivec3 tangents[2];
    int t = 0;
    for (int axis = 0; axis < 3; axis++) {
        if (normal[axis] != 0) continue;
        tangents[t++] = glm::ivec3(axis == 0, axis == 1, axis == 2);
    }
 
    // Determine sign along each tangent from the corner offset
    // cornerOffset values are 0 or 1; map to -1 / +1 relative to block centre
    glm::ivec3 s1 = tangents[0] * (static_cast<int>(cornerOffset[tangents[0].x != 0 ? 0 : tangents[0].y != 0 ? 1 : 2]) * 2 - 1);
    glm::ivec3 s2 = tangents[1] * (static_cast<int>(cornerOffset[tangents[1].x != 0 ? 0 : tangents[1].y != 0 ? 1 : 2]) * 2 - 1);

    glm::ivec3 n = normal;
    glm::ivec3 base = blockPos + n;  // step out of the block along the normal
 
    bool side1  = IsOpaque(chunk.GetBlock(base.x + s1.x, base.y + s1.y, base.z + s1.z));
    bool side2  = IsOpaque(chunk.GetBlock(base.x + s2.x, base.y + s2.y, base.z + s2.z));
    bool corner = IsOpaque(chunk.GetBlock(base.x + s1.x + s2.x, base.y + s1.y + s2.y, base.z + s1.z + s2.z));
 
    if (side1 && side2)
        return 0;  // fully occluded
 
    return 3u - (static_cast<uint32_t>(side1) + static_cast<uint32_t>(side2) + static_cast<uint32_t>(corner));
}

///
/// Mesher
///

void ChunkMesher::Mesh(const Chunk& chunk, std::vector<VoxelVertex>& outVertices,
    std::vector<uint32_t>& outIndices, float atlasCols, float atlasRows)
{
    outVertices.clear();
    outIndices.clear();
 
    const float tileW = 1.0f / atlasCols;
    const float tileH = 1.0f / atlasRows;

    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int y = 0; y < CHUNK_SIZE; y++) {
            for (int x = 0; x < CHUNK_SIZE; x++) {
                BlockType block = chunk.GetBlock(x, y, z);
                if (!IsOpaque(block)) continue;
 
                const BlockInfo& info = BLOCK_TABLE[static_cast<uint8_t>(block)];
 
                // UV origin for this block's atlas tile
                float uBase = info.atlasCol * tileW;
                float vBase = info.atlasRow * tileH;
 
                glm::vec3 blockOrigin{ x, y, z };
                glm::ivec3 iPos{ x, y, z };

                for (const FaceDef& face : FACES) {
                    // Neighbour in the direction of this face
                    glm::ivec3 nb = iPos + face.normal;
                    if (IsOpaque(chunk.GetBlock(nb.x, nb.y, nb.z)))
                        continue; // neighbour is solid — face is hidden
 
                    // Emit 4 vertices
                    uint32_t base = static_cast<uint32_t>(outVertices.size());
 
                    uint32_t ao[4];
                    for (int c = 0; c < 4; c++) {
                        ao[c] = VertexAO(chunk, iPos, face.normal, face.corners[c]);
                    }
 
                    for (int c = 0; c < 4; c++) {
                        VoxelVertex v{};
                        v.position  = blockOrigin + face.corners[c];
                        v.uv        = { uBase + face.uvs[c].x * tileW,
                                        vBase + face.uvs[c].y * tileH };
                        v.faceIndex = face.faceIndex;
                        v.ao        = ao[c];
                        outVertices.push_back(v);
                    }

                    // ── AO-correct quad flip ──────────────────
                    // Flip the diagonal of the quad based on AO values
                    // to avoid the interpolation artefact ("AO seam")
                    // that Minecraft uses.  Standard rule:
                    //   if ao[0]+ao[2] < ao[1]+ao[3]  →  flip
                    bool flip = (ao[0] + ao[2]) < (ao[1] + ao[3]);
 
                    if (!flip) {
                        outIndices.push_back(base + 0);
                        outIndices.push_back(base + 1);
                        outIndices.push_back(base + 2);
                        outIndices.push_back(base + 0);
                        outIndices.push_back(base + 2);
                        outIndices.push_back(base + 3);
                    } else {
                        outIndices.push_back(base + 1);
                        outIndices.push_back(base + 2);
                        outIndices.push_back(base + 3);
                        outIndices.push_back(base + 1);
                        outIndices.push_back(base + 3);
                        outIndices.push_back(base + 0);
                    }
                }
            }
        }
    }
}