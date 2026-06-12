#include "ChunkManager.hpp"
#include "engine/renderer/Renderer.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/core/Logger.hpp"
#include "engine/world/Chunk.hpp"
#include "engine/world/ChunkMesh.hpp"

// ─────────────────────────────────────────────
//  Coordinate helpers
// ─────────────────────────────────────────────
 
// Floor-division that works correctly for negative numerators.
static int FloorDiv(int a, int b) {
    return a / b - (a % b != 0 && (a ^ b) < 0);
}
 
// Positive modulo.
static int PosMod(int a, int b) {
    return ((a % b) + b) % b;
}
 
glm::ivec3 ChunkManager::WorldToChunkCoord(glm::ivec3 wp) {
    return {
        FloorDiv(wp.x, CHUNK_SIZE),
        FloorDiv(wp.y, CHUNK_SIZE),
        FloorDiv(wp.z, CHUNK_SIZE)
    };
}

glm::ivec3 ChunkManager::WorldToLocalPos(glm::ivec3 wp) {
    return {
        PosMod(wp.x, CHUNK_SIZE),
        PosMod(wp.y, CHUNK_SIZE),
        PosMod(wp.z, CHUNK_SIZE)
    };
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
 
ChunkManager::ChunkManager(VulkanContext* context, Renderer* renderer)
    : m_context(context), m_renderer(renderer) {}
 
ChunkManager::~ChunkManager() {
    VkDevice dev = m_context->GetDevice();
    for (auto& [coord, chunk] : m_chunks)
        chunk->DestroyBuffers(dev);
}

void ChunkManager::Init(VkCommandPool pool, VkQueue queue) {
    // Seed the world with a modest flat grid of chunks at Y-slab 0.
    // Update() will expand this as the camera moves.
    constexpr int SEED_RADIUS = 4;
    for (int cz = -SEED_RADIUS; cz <= SEED_RADIUS; ++cz)
        for (int cx = -SEED_RADIUS; cx <= SEED_RADIUS; ++cx) {
            glm::ivec3 coord{ cx, 0, cz };
            auto chunk = std::make_unique<Chunk>();
            chunk->m_chunkCoord = coord;
            GenerateChunk(*chunk, coord);
            m_chunks.emplace(coord, std::move(chunk));
        }
 
    FlushDirty(pool, queue);
    Logger::Log(LogLevel::Info, "World", "Initial chunks generated and uploaded");
}

// ─────────────────────────────────────────────
//  Per-frame Update
// ─────────────────────────────────────────────

void ChunkManager::Update(glm::vec3 cameraWorldPos, int viewRadiusXZ, VkCommandPool pool, VkQueue queue) {
    // Camera's XZ chunk coordinate (Y always 0 - we only have one slab layer)
    const glm::ivec3 camChunk {
        FloorDiv(static_cast<int>(std::floor(cameraWorldPos.x)), CHUNK_SIZE),
        0,
        FloorDiv(static_cast<int>(std::floor(cameraWorldPos.z)), CHUNK_SIZE)
    };

    // Load missing chunks inside radius
    const int r = viewRadiusXZ;
    for (int dz = -r; dz <= r; ++dz)
    for (int dx = -r; dx <= r; ++dx) {
        if (dx*dx + dz*dz > r*r) continue;   // circular radius
        glm::ivec3 coord { camChunk.x + dx, 0, camChunk.z + dz };
        if (m_chunks.count(coord)) continue;
 
        auto chunk = std::make_unique<Chunk>();
        chunk->m_chunkCoord = coord;
        GenerateChunk(*chunk, coord);
        m_chunks.emplace(coord, std::move(chunk));
    }

    // Unload chunks beyond radius + margin
    const int unloadR = r + 2;
    std::vector<glm::ivec3> toRemove;
    for (auto& [coord, _] : m_chunks) {
        int dx = coord.x - camChunk.x;
        int dz = coord.z - camChunk.z;
        if (dx*dx + dz*dz > unloadR*unloadR)
            toRemove.push_back(coord);
    }
    if (!toRemove.empty()) {
        VkDevice dev = m_context->GetDevice();
        for (auto& coord : toRemove) {
            m_chunks[coord]->DestroyBuffers(dev);
            m_chunks.erase(coord);
        }
        Logger::Log(LogLevel::Info, "World", "Unloaded " + std::to_string(toRemove.size()) + " chunk(s)");
    }
 
    FlushDirty(pool, queue);
}

// ─────────────────────────────────────────────
//  Mesh management
// ─────────────────────────────────────────────
 
void ChunkManager::FlushDirty(VkCommandPool pool, VkQueue queue)
{
    for (auto& [coord, chunk] : m_chunks)
        if (chunk->m_dirty)
            RebuildMesh(*chunk, pool, queue);
}

void ChunkManager::RebuildMesh(Chunk& chunk, VkCommandPool pool, VkQueue queue) {
    ChunkNeighbors nb = GatherNeighbors(chunk.m_chunkCoord);
 
    // Determine which slabs to remesh.
    // m_dirty means "rebuild all" (initial gen, neighbor-caused full remesh).
    // Otherwise only the slabs flagged in the dirty bitmask.
    uint8_t slabMask = chunk.m_dirty
        ? (1u << SLAB_COUNT) - 1u   // all 4 slabs
        : chunk.GetDirtySlabs();

    std::vector<VoxelVertex> verts;
    std::vector<uint32_t> indices;

    for (int slab = 0; slab < SLAB_COUNT; ++slab) {
        if (!((slabMask >> slab) & 1u)) continue;

        const int yMin = slab * SLAB_HEIGHT;
        const int yMax = slab * SLAB_HEIGHT;

        // Append this slab's geometry into the shared buffers.
        // Index values must be offset by however many vertices
        // were already written by earlier slabs.
        const uint32_t vertexBase = static_cast<uint32_t>(verts.size());

        std::vector<VoxelVertex> slabVerts;
        std::vector<uint32_t>    slabIndices;
        ChunkMesher::Mesh(chunk, nb, slabVerts, slabIndices, yMin, yMax);

        for (auto& v : slabVerts)
            verts.push_back(v);

        for (uint32_t idx : slabIndices)
            indices.push_back(idx + vertexBase);

        chunk.ClearSlabDirty(slab);
    }

    chunk.m_dirty = false;
    chunk.UploadMesh(m_context->GetDevice(), m_context->GetPhysicalDevice(),
        pool, queue, verts, indices);

    Logger::Log(LogLevel::Info, "World",
        "Meshed (" +
        std::to_string(chunk.m_chunkCoord.x) + "," +
        std::to_string(chunk.m_chunkCoord.z) + ") slabMask=" +
        std::to_string(slabMask) + " — " +
        std::to_string(verts.size())       + " verts, " +
        std::to_string(indices.size() / 3) + " tris");
}

// ─────────────────────────────────────────────
//  Chunk lookup / neighbor gather
// ─────────────────────────────────────────────
 
Chunk* ChunkManager::GetChunk(glm::ivec3 coord) {
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}
 
const Chunk* ChunkManager::GetChunk(glm::ivec3 coord) const {
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}
 
ChunkNeighbors ChunkManager::GatherNeighbors(glm::ivec3 cc) const {
    return {
        GetChunk({cc.x+1, cc.y,   cc.z  }),   // +X
        GetChunk({cc.x-1, cc.y,   cc.z  }),   // -X
        GetChunk({cc.x,   cc.y+1, cc.z  }),   // +Y
        GetChunk({cc.x,   cc.y-1, cc.z  }),   // -Y
        GetChunk({cc.x,   cc.y,   cc.z+1}),   // +Z
        GetChunk({cc.x,   cc.y,   cc.z-1}),   // -Z
    };
}

// ─────────────────────────────────────────────
//  World generation
// ─────────────────────────────────────────────
 
void ChunkManager::GenerateChunk(Chunk& chunk, glm::ivec3 /*chunkCoord*/) const {
    // Flat stone world.  Terrain generation goes here later.
    // Layout inside the 8-block tall slab:
    //   y 0-4  Stone
    //   y 5    Dirt
    //   y 6    Dirt
    //   y 7    Grass  (surface — top face is fully exposed to air above)
    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                BlockType t = BlockType::Air;
                if (y <= 4) t = BlockType::Stone;
                else if (y <= 6) t = BlockType::Dirt;
                else t = BlockType::Grass;
                chunk.SetBlock(x, y, z, t);
            }
    chunk.m_dirty = true;
}

// ─────────────────────────────────────────────
//  World block access
// ─────────────────────────────────────────────
 
BlockType ChunkManager::GetBlock(glm::ivec3 wp) const {
    const Chunk* c = GetChunk(WorldToChunkCoord(wp));
    if (!c) return BlockType::Air;
    auto lp = WorldToLocalPos(wp);
    return c->GetBlock(lp.x, lp.y, lp.z);
}
 
void ChunkManager::SetBlock(glm::ivec3 wp, BlockType type) {
    glm::ivec3 cc = WorldToChunkCoord(wp);
    Chunk* c = GetChunk(cc);
    if (!c) return;
 
    auto lp = WorldToLocalPos(wp);
    c->SetBlock(lp.x, lp.y, lp.z, type);   // sets m_dirty = true on the chunk
 
    // If the edited block is on a face shared with a neighbor, that
    // neighbor must also re-mesh so it can cull (or un-cull) that face.
    auto markNeighbor = [&](int dx, int dy, int dz, bool onBorder) {
        if (!onBorder) return;
        Chunk* nb = GetChunk({cc.x + dx, cc.y + dy, cc.z + dz});
        if (nb) nb->m_dirty = true;
    };
 
    markNeighbor(-1, 0, 0, lp.x == 0);
    markNeighbor(+1, 0, 0, lp.x == CHUNK_SIZE - 1);
    markNeighbor( 0,-1, 0, lp.y == 0);
    markNeighbor( 0,+1, 0, lp.y == CHUNK_SIZE - 1);
    markNeighbor( 0, 0,-1, lp.z == 0);
    markNeighbor( 0, 0,+1, lp.z == CHUNK_SIZE - 1);
}

// ─────────────────────────────────────────────
//  Raycast  (Amanatides & Woo DDA)
//
//  Traverses one voxel face at a time along the
//  ray.  At each step we record which face we
//  just crossed so that on a hit we can report
//  the outward normal (for block placement).
// ─────────────────────────────────────────────
 
RaycastResult ChunkManager::Raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance) const {
    RaycastResult result;
 
    const float len = glm::length(dir);
    if (len < 1e-6f) return result;
    dir /= len;
 
    // Current voxel (floor to handle negative coords correctly)
    glm::ivec3 voxel {
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z))
    };
 
    // Per-axis step direction
    const glm::ivec3 step {
        dir.x >= 0.f ? 1 : -1,
        dir.y >= 0.f ? 1 : -1,
        dir.z >= 0.f ? 1 : -1
    };

    // Distance along ray to the next voxel boundary on each axis,
    // and the distance to cross one full voxel on each axis.
    auto initT = [](float orig, float d, int s) -> float {
        if (std::abs(d) < 1e-6f) return 1e30f;
        float boundary = s > 0 ? std::floor(orig) + 1.f : std::ceil (orig) - 1.f;
        return (boundary - orig) / d;
    };

    glm::vec3 tMax {
        initT(origin.x, dir.x, step.x),
        initT(origin.y, dir.y, step.y),
        initT(origin.z, dir.z, step.z)
    };
 
    const glm::vec3 tDelta {
        std::abs(dir.x) > 1e-6f ? std::abs(1.f / dir.x) : 1e30f,
        std::abs(dir.y) > 1e-6f ? std::abs(1.f / dir.y) : 1e30f,
        std::abs(dir.z) > 1e-6f ? std::abs(1.f / dir.z) : 1e30f
    };
 
    glm::ivec3 lastNormal { 0, 0, 0 };
    float dist = 0.f;

    while (dist < maxDistance) {
        if (IsOpaque(GetBlock(voxel))) {
            result.hit = true;
            result.blockPos = voxel;
            result.faceNormal = lastNormal;
            result.distance = dist;
            return result;
        }
 
        // Advance to the nearest next boundary
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            dist = tMax.x;
            tMax.x += tDelta.x;
            lastNormal = { -step.x, 0, 0 };
            voxel.x += step.x;
        } else if (tMax.y < tMax.z) {
            dist = tMax.y;
            tMax.y += tDelta.y;
            lastNormal = { 0, -step.y, 0 };
            voxel.y += step.y;
        } else {
            dist = tMax.z;
            tMax.z += tDelta.z;
            lastNormal = { 0, 0, -step.z };
            voxel.z += step.z;
        }
    }

    return result;
}