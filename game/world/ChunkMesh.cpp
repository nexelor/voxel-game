#include "game/world/ChunkMesh.hpp"
#include "game/registry/BlockRegistry.hpp"

#include <glm/glm.hpp>

// ─────────────────────────────────────────────
//  ChunkNeighbors::Sample
//
//  Called only when (x,y,z) is one step outside
//  the chunk on exactly one axis.
// ─────────────────────────────────────────────
 
BlockType ChunkNeighbors::Sample(int x, int y, int z) const {
    if (x < 0)            return nx ? nx->GetBlock(x + CHUNK_SIZE, y, z) : BlockType::Air;
    if (x >= CHUNK_SIZE)  return px ? px->GetBlock(x - CHUNK_SIZE, y, z) : BlockType::Air;
    if (y < 0)            return ny ? ny->GetBlock(x, y + CHUNK_SIZE, z) : BlockType::Air;
    if (y >= CHUNK_SIZE)  return py ? py->GetBlock(x, y - CHUNK_SIZE, z) : BlockType::Air;
    if (z < 0)            return nz ? nz->GetBlock(x, y, z + CHUNK_SIZE) : BlockType::Air;
    if (z >= CHUNK_SIZE)  return pz ? pz->GetBlock(x, y, z - CHUNK_SIZE) : BlockType::Air;
    return BlockType::Air;   // unreachable in normal use
}

// ─────────────────────────────────────────────
//  Face table
//
//  Counter-clockwise winding from outside the
//  block, matching VK_FRONT_FACE_COUNTER_CLOCKWISE
//  with back-face culling.
//
//  Corner layout (same across all 6 faces):
//      0 ── 1
//      │  ╲ │    tri0 = 0,1,2    tri1 = 0,2,3
//      3 ── 2
//
//  `index` doubles as both the shader's
//  FACE_BRIGHTNESS table index AND the index into
//  BlockDef::faceTextures / faceAtlasCoords — both
//  tables are laid out in this exact order
//  (0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z), so the mesher
//  can use it directly for both lookups.
// ─────────────────────────────────────────────
 
struct FaceDef {
    uint32_t   index;       // shared FACE_BRIGHTNESS / faceTextures index
    glm::ivec3 normal;      // integer step to neighbor block
    glm::vec3  corners[4];  // vertex offsets from block origin
    glm::vec2  uvs[4];      // local [0,1]² UVs
};

static constexpr FaceDef FACES[6] = {
    // +X  east
    { 0, { 1, 0, 0},
      {{ 1,1,0},{ 1,1,1},{ 1,0,1},{ 1,0,0}},
      {{ 0,0}, { 1,0}, { 1,1}, { 0,1}} },
    // -X  west
    { 1, {-1, 0, 0},
      {{ 0,1,1},{ 0,1,0},{ 0,0,0},{ 0,0,1}},
      {{ 0,0}, { 1,0}, { 1,1}, { 0,1}} },
    // +Y  top
    { 2, { 0, 1, 0},
      {{ 0,1,1},{ 1,1,1},{ 1,1,0},{ 0,1,0}},
      {{ 0,0}, { 1,0}, { 1,1}, { 0,1}} },
    // -Y  bottom
    { 3, { 0,-1, 0},
      {{ 0,0,0},{ 1,0,0},{ 1,0,1},{ 0,0,1}},
      {{ 0,0}, { 1,0}, { 1,1}, { 0,1}} },
    // +Z  south
    { 4, { 0, 0, 1},
      {{ 0,1,1},{ 0,0,1},{ 1,0,1},{ 1,1,1}},
      {{ 0,0}, { 0,1}, { 1,1}, { 1,0}} },
    // -Z  north
    { 5, { 0, 0,-1},
      {{ 1,1,0},{ 1,0,0},{ 0,0,0},{ 0,1,0}},
      {{ 0,0}, { 0,1}, { 1,1}, { 1,0}} },
};

// ─────────────────────────────────────────────
//  Helper: resolve a block that may be in this
//  chunk or a neighbor
// ─────────────────────────────────────────────
 
static inline BlockType QueryBlock(const Chunk& chunk, const ChunkNeighbors& nb, int x, int y, int z) {
    if (Chunk::InBounds(x, y, z))
        return chunk.GetBlock(x, y, z);
    return nb.Sample(x, y, z);
}
 
// ─────────────────────────────────────────────
//  Vertex AO
//
//  Standard Minecraft-style per-vertex AO.
//  For a face corner we sample three neighbors
//  (side1, side2, diagonal corner) one step
//  outside the face in the plane of that face.
//  Returns 0 (most occluded) .. 3 (open).
// ─────────────────────────────────────────────
 
static uint32_t VertexAO(const Chunk& chunk, const ChunkNeighbors& nb,
    glm::ivec3 blockPos, glm::ivec3 faceNormal, glm::vec3 cornerOffset)
{
    glm::ivec3 tangents[2];
    int t = 0;
    for (int axis = 0; axis < 3; ++axis) {
        if (faceNormal[axis] != 0) continue;
        glm::ivec3 tv(0);
        tv[axis] = 1;
        tangents[t++] = tv;
    }
 
    auto axisOf = [](glm::ivec3 tv) -> int {
        return tv.x ? 0 : tv.y ? 1 : 2;
    };
    glm::ivec3 s1 = tangents[0] * (static_cast<int>(cornerOffset[axisOf(tangents[0])]) * 2 - 1);
    glm::ivec3 s2 = tangents[1] * (static_cast<int>(cornerOffset[axisOf(tangents[1])]) * 2 - 1);
 
    glm::ivec3 base = blockPos + faceNormal;
 
    bool side1  = ContributesToAO(QueryBlock(chunk, nb, base.x+s1.x,       base.y+s1.y,       base.z+s1.z      ));
    bool side2  = ContributesToAO(QueryBlock(chunk, nb, base.x+s2.x,       base.y+s2.y,       base.z+s2.z      ));
    bool corner = ContributesToAO(QueryBlock(chunk, nb, base.x+s1.x+s2.x,  base.y+s1.y+s2.y,  base.z+s1.z+s2.z ));
 
    if (side1 && side2) return 0;
 
    return 3u - (static_cast<uint32_t>(side1)
               + static_cast<uint32_t>(side2)
               + static_cast<uint32_t>(corner));
}

// ─────────────────────────────────────────────
//  ChunkMesher::Mesh
//
//  atlasCols/atlasRows now describe the REAL grid
//  dimensions of the built TextureAtlas (e.g. from
//  TextureAtlas::GetGridCols()/GetGridRows()), and
//  each face's tile offset comes from the block's
//  resolved BlockDef::faceAtlasCoords — populated by
//  BlockRegistry::ResolveAtlasCoords() right after
//  the atlas is built (see Application::Initialize).
// ─────────────────────────────────────────────
 
void ChunkMesher::Mesh(const Chunk& chunk, const ChunkNeighbors& neighbors, ChunkMeshData& out,
    int yMin, int yMax, float atlasCols, float atlasRows)
{
    out.solidVertices.clear();
    out.solidIndices.clear();
    out.translucentVertices.clear();
    out.translucentIndices.clear();
 
    const float tileW = 1.0f / atlasCols;
    const float tileH = 1.0f / atlasRows;
 
    const BlockRegistry& registry = BlockRegistry::Get();

    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int y = yMin; y < yMax; ++y)
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                const BlockType block = chunk.GetBlock(x, y, z);
                if (block == BlockType::Air) continue;

                const BlockDef& def = registry.Lookup(block);
 
                // Route this block's faces into the right buffer pair.
                const bool translucent = (def.renderLayer == RenderLayer::Translucent);
                auto& outVerts = translucent ? out.translucentVertices : out.solidVertices;
                auto& outIdx   = translucent ? out.translucentIndices  : out.solidIndices;

                const glm::vec3  origin { static_cast<float>(x),
                                          static_cast<float>(y),
                                          static_cast<float>(z) };
                const glm::ivec3 ipos   { x, y, z };
                
                for (const FaceDef& face : FACES) {
                    const glm::ivec3 nb = ipos + face.normal;
                    if (FaceHidden(block, QueryBlock(chunk, neighbors, nb.x, nb.y, nb.z)))
                        continue;
 
                    // Per-face atlas tile, resolved from this block's TextureID
                    // for face index `face.index` (0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z).
                    const auto& tileCoord = def.faceAtlasCoords[face.index];
                    const float uBase = static_cast<float>(tileCoord.first) * tileW;
                    const float vBase = static_cast<float>(tileCoord.second) * tileH;
                
                    const uint32_t base = static_cast<uint32_t>(outVerts.size());
                
                    // Per-corner AO
                    uint32_t ao[4];
                    for (int c = 0; c < 4; ++c)
                        ao[c] = VertexAO(chunk, neighbors, ipos, face.normal, face.corners[c]);
                
                    // 4 vertices
                    for (int c = 0; c < 4; ++c) {
                        VoxelVertex v{};
                        v.position  = origin + face.corners[c];
                        v.uv        = { uBase + face.uvs[c].x * tileW, vBase + face.uvs[c].y * tileH };
                        v.faceIndex = face.index;
                        v.ao        = ao[c];
                        outVerts.push_back(v);
                    }

                    // AO-correct quad flip (avoids interpolation seam)
                    // Flip when the ao[0]+ao[2] diagonal is darker than ao[1]+ao[3]
                    const bool flip = (ao[0] + ao[2]) < (ao[1] + ao[3]);
                    if (!flip) {
                        outIdx.push_back(base + 0); outIdx.push_back(base + 1);
                        outIdx.push_back(base + 2); outIdx.push_back(base + 0);
                        outIdx.push_back(base + 2); outIdx.push_back(base + 3);
                    } else {
                        outIdx.push_back(base + 1); outIdx.push_back(base + 2);
                        outIdx.push_back(base + 3); outIdx.push_back(base + 1);
                        outIdx.push_back(base + 3); outIdx.push_back(base + 0);
                    }
                }
            }
}