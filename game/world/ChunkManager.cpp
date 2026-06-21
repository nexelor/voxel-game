#include "game/world/ChunkManager.hpp"
#include "engine/renderer/Renderer.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/core/Logger.hpp"
#include "game/world/Chunk.hpp"
#include "game/world/ChunkMesh.hpp"
#include <algorithm>
#include <cstdlib>

static constexpr int VERTICAL_RENDER_DISTANCE = 3; // slabs above/below camera chunk

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

// ─────────────────────────────────────────────
//  MarkExistingNeighborsDirty
//
//  When a brand-new chunk appears at `coord`, any
//  ALREADY-LOADED neighbor chunk has a mesh that
//  was built believing this side was empty space
//  (neighbor == nullptr => treated as air in the
//  mesher). That neighbor's border faces are now
//  potentially hidden behind the new chunk's
//  blocks, so it must be re-meshed.
//
//  This mirrors the markNeighbor() logic already
//  used in SetBlock() for individual block edits —
//  chunk generation was missing the equivalent step,
//  which is what let the seam between an old chunk
//  and a newly streamed-in chunk go uncull
// ─────────────────────────────────────────────

void ChunkManager::MarkExistingNeighborsDirty(glm::ivec3 coord) {
    static constexpr glm::ivec3 kOffsets[6] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1}
    };
 
    for (const auto& off : kOffsets) {
        Chunk* neighbor = GetChunk(coord + off);
        if (neighbor) neighbor->m_dirty = true;
    }
}

void ChunkManager::Init(VkCommandPool pool, VkQueue queue) {
    // Seed the world with a modest flat grid of chunks at Y-slab 0.
    // Update() will expand this as the camera moves.
    constexpr int SEED_RADIUS = 4;
    for (int cz = -SEED_RADIUS; cz <= SEED_RADIUS; ++cz)
        for (int cx = -SEED_RADIUS; cx <= SEED_RADIUS; ++cx)
            for (int cy = 0; cy < WORLD_HEIGHT_CHUNKS; ++cy) {
                glm::ivec3 coord{ cx, cy, cz };
                auto chunk = std::make_unique<Chunk>();
                chunk->m_chunkCoord = coord;
                GenerateChunk(*chunk, coord);
                m_chunks.emplace(coord, std::move(chunk));

                MarkExistingNeighborsDirty(coord);
            }
 
    FlushDirty(SEA_LEVEL_CHUNK, pool, queue);
    Logger::Log(LogLevel::Info, "World", "Initial chunks generated and uploaded");
}

// ─────────────────────────────────────────────
//  Per-frame Update
// ─────────────────────────────────────────────

void ChunkManager::Update(glm::vec3 cameraWorldPos, int viewRadiusXZ, VkCommandPool pool, VkQueue queue) {
    const int camChunkX = FloorDiv(static_cast<int>(std::floor(cameraWorldPos.x)), CHUNK_SIZE);
    const int camChunkY = std::clamp(
        FloorDiv(static_cast<int>(std::floor(cameraWorldPos.y)), CHUNK_SIZE),
        0, WORLD_HEIGHT_CHUNKS - 1
    );
    const int camChunkZ = FloorDiv(static_cast<int>(std::floor(cameraWorldPos.z)), CHUNK_SIZE);

    // Load missing chunks — full Y column for every XZ position in radius
    const int r = viewRadiusXZ;
    for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx) {
            if (dx*dx + dz*dz > r*r) continue;
            for (int cy = 0; cy < WORLD_HEIGHT_CHUNKS; ++cy) {
                glm::ivec3 coord { camChunkX + dx, cy, camChunkZ + dz };
                if (m_chunks.count(coord)) continue;
                auto chunk = std::make_unique<Chunk>();
                chunk->m_chunkCoord = coord;
                GenerateChunk(*chunk, coord);
                m_chunks.emplace(coord, std::move(chunk));

                MarkExistingNeighborsDirty(coord);
            }
        }

    // Unload XZ columns beyond radius + margin (unload all Y slabs for that column)
    const int unloadR = r + 2;
    std::vector<glm::ivec3> toRemove;
    for (auto& [coord, _] : m_chunks) {
        int dx = coord.x - camChunkX;
        int dz = coord.z - camChunkZ;
        if (dx*dx + dz*dz > unloadR*unloadR)
            toRemove.push_back(coord);
    }
    if (!toRemove.empty()) {
        VkDevice dev = m_context->GetDevice();
        for (auto& coord : toRemove) {
            m_chunks[coord]->DestroyBuffers(dev);
            m_chunks.erase(coord);
        }
        Logger::Log(LogLevel::Info, "World",
            "Unloaded " + std::to_string(toRemove.size()) + " chunk(s)");
    }

    FlushDirty(camChunkY, pool, queue);
}

// ─────────────────────────────────────────────
//  Mesh management
// ─────────────────────────────────────────────
 
void ChunkManager::FlushDirty(int camChunkY, VkCommandPool pool, VkQueue queue) {
    for (auto& [coord, chunk] : m_chunks) {
        if (!chunk->m_dirty) continue;

        const int dy = std::abs(coord.y - camChunkY);
        if (dy > VERTICAL_RENDER_DISTANCE) { continue; }
        
        RebuildMesh(*chunk, pool, queue);
    }
}

void ChunkManager::RebuildMesh(Chunk& chunk, VkCommandPool pool, VkQueue queue) {
    ChunkNeighbors nb = GatherNeighbors(chunk.m_chunkCoord);

    std::vector<VoxelVertex> verts;
    std::vector<uint32_t>    indices;

    ChunkMesher::Mesh(chunk, nb, verts, indices, 0, CHUNK_SIZE,
        static_cast<float>(m_atlasGridCols), static_cast<float>(m_atlasGridRows));

    chunk.ClearAllDirty();
    chunk.UploadMesh(m_context->GetDevice(), m_context->GetPhysicalDevice(),
        pool, queue, verts, indices);

    Logger::Log(LogLevel::Info, "World",
        "Meshed (" +
        std::to_string(chunk.m_chunkCoord.x) + "," +
        std::to_string(chunk.m_chunkCoord.z) + ") — " +
        std::to_string(verts.size()) + " verts, " +
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
 
void ChunkManager::GenerateChunk(Chunk& chunk, glm::ivec3 chunkCoord) const {
    // World-space Y of the bottom block in this slab
    const int worldYBase = chunkCoord.y * CHUNK_SIZE;

    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                const int worldY = worldYBase + y;
            
                BlockType t = BlockType::Air;
            
                if (worldY < SEA_LEVEL - 3) {
                    t = BlockType::Stone;
                } else if (worldY < SEA_LEVEL - 1) {
                    t = BlockType::Dirt;
                } else if (worldY == SEA_LEVEL - 1) {
                    t = BlockType::Grass;
                }
                // worldY >= SEA_LEVEL → Air (sky above the surface)
            
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