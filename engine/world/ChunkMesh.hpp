#pragma once

#include "Chunk.hpp"
#include "engine/renderer/VoxelVertex.hpp"

#include <vector>

// ─────────────────────────────────────────────
//  ChunkMesher
//
//  Naive face-culled mesher.  For each solid
//  block it emits only the faces whose neighbour
//  is Air (or out-of-bounds).  This produces
//  correct output with no overdraw while being
//  trivially simple to understand and extend.
//
//  Call:
//      ChunkMesher::Mesh(chunk, vertices, indices);
//  then:
//      chunk.UploadMesh(device, physDevice, pool, queue, vertices, indices);
//
//  UV layout: the atlas tile is chosen from
//  BLOCK_TABLE. atlasCols / atlasRows describe
//  the full atlas dimensions (e.g. 16×16 tiles).
//  The mesher bakes the sub-tile UVs into each
//  vertex, so the shader just does texture().
// ─────────────────────────────────────────────

class ChunkMesher {
public:
    static void Mesh(
        const Chunk& chunk,
        std::vector<VoxelVertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        float atlasCols = 16.0f,
        float atlasRows = 16.0f
    );
};