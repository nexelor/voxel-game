#pragma once

#include <cstdint>

// ─────────────────────────────────────────────
//  Block
//
//  A block is a single uint8 stored inside a
//  Chunk. 0 = Air (empty). Everything else is
//  a solid block type.
//
//  atlasRow / atlasCol encode which tile inside
//  the texture atlas to use for this block.
//  For now every face of a block uses the same
//  tile; add per-face overrides later if needed.
// ─────────────────────────────────────────────
 
enum class BlockType : uint8_t {
    Air   = 0,
    Grass = 1,
    Dirt  = 2,
    Stone = 3,
    Sand  = 4,
};

struct BlockInfo {
    uint8_t atlasRow;
    uint8_t atlasCol;
};
 
// Indexed by BlockType (cast to uint8_t).
// Air entry is never used but keeps indexing trivial.
inline constexpr BlockInfo BLOCK_TABLE[] = {
    { 0, 0 },  // Air
    { 0, 0 },  // Grass  — row 0, col 0
    { 0, 1 },  // Dirt   — row 0, col 1
    { 0, 2 },  // Stone  — row 0, col 2
    { 0, 3 },  // Sand   — row 0, col 3
};
 
inline bool IsOpaque(BlockType t) {
    return t != BlockType::Air;
}