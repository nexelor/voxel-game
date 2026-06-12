#pragma once
#include "game/registry/BlockRegistry.hpp"

inline void RegisterAllBlocks(BlockRegistry& reg) {
    reg.Register(BlockType::Air,   { "air",   0, 0, false });
    reg.Register(BlockType::Grass, { "grass", 0, 0, true  });
    reg.Register(BlockType::Dirt,  { "dirt",  0, 1, true  });
    reg.Register(BlockType::Stone, { "stone", 0, 2, true  });
    reg.Register(BlockType::Sand,  { "sand",  0, 3, true  });
}