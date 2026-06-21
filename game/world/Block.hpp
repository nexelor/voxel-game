#pragma once

#include <cstdint>

// ─────────────────────────────────────────────
//  Block
//
//  A block is a single uint8 stored inside a
//  Chunk. 0 = Air (empty). Everything else is
//  a solid block type.
//
//  Per-block texture/appearance data (which PNG
//  each face uses) now lives entirely in
//  BlockRegistry/BlockDef (game/registry/BlockRegistry.hpp),
//  resolved against the runtime-built TextureAtlas.
//  This header only owns the type enum and the
//  one opacity rule everything else is built on.
// ─────────────────────────────────────────────
 
enum class BlockType : uint8_t {
    Air   = 0,
    Grass = 1,
    Dirt  = 2,
    Stone = 3,
    Sand  = 4,
    Glass  = 5,
};

inline bool IsOpaque(BlockType t) {
    return t != BlockType::Air;
}