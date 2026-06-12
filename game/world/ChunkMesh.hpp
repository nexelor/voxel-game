#pragma once

#include "game/world/Chunk.hpp"
#include "engine/renderer/VoxelVertex.hpp"

#include <vector>

// ─────────────────────────────────────────────
//  ChunkNeighbors
//
//  Six optional adjacent chunks passed into the
//  mesher so faces on chunk borders can be
//  correctly culled against the neighbor's data.
//
//  A nullptr means "treat as air" — correct for
//  world edges and not-yet-loaded chunks (we
//  show the face rather than hiding it into void).
//
//  Pointer order matches the face-index table in
//  the mesher: +X, −X, +Y, −Y, +Z, −Z.
// ─────────────────────────────────────────────

struct ChunkNeighbors {
    const Chunk* px = nullptr;   // +X  (east)
    const Chunk* nx = nullptr;   // −X  (west)
    const Chunk* py = nullptr;   // +Y  (above)
    const Chunk* ny = nullptr;   // −Y  (below)
    const Chunk* pz = nullptr;   // +Z  (south)
    const Chunk* nz = nullptr;   // −Z  (north)
 
    // Resolve a chunk-local coordinate that has spilled exactly
    // one step outside [0,SIZE) on one axis into the right neighbor.
    // Coordinates out-of-range on more than one axis never occur in
    // a face-based mesher (each face normal has exactly one nonzero
    // component).
    BlockType Sample(int x, int y, int z) const;
};

// ─────────────────────────────────────────────
//  ChunkMesher
//
//  Naive face-culled mesher — O(V) where V is
//  the number of visible faces.
//
//  Usage:
//    ChunkNeighbors nb = manager.GatherNeighbors(coord);
//    ChunkMesher::Mesh(chunk, nb, verts, indices);
//    chunk.UploadMesh(..., verts, indices);
// ─────────────────────────────────────────────
 
class ChunkMesher {
public:
    static void Mesh(
        const Chunk& chunk,
        const ChunkNeighbors& neighbors,
        std::vector<VoxelVertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int yMin = 0,
        int yMax = CHUNK_SIZE,
        float atlasCols = 16.0f,
        float atlasRows = 16.0f
    );
};