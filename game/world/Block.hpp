#pragma once

#include <cstdint>

// ─────────────────────────────────────────────
//  Block
//
//  A block is a single uint8 stored inside a
//  Chunk. 0 = Air (empty). Everything else is
//  a block type.
//
//  This header owns ONLY the type enum. Every
//  question about how a block looks or behaves
//  (textures, render layer, whether it occludes
//  neighbors) lives in BlockRegistry/BlockDef
//  (game/registry/BlockRegistry.hpp) — kept out
//  of here on purpose so this header never needs
//  to depend on the registry.
// ─────────────────────────────────────────────
 
enum class BlockType : uint8_t {
    Air   = 0,
    Grass = 1,
    Dirt  = 2,
    Stone = 3,
    Sand  = 4,
    Glass  = 5,
    Leaves = 6,
    Water = 7,
};