#pragma once
#include "game/world/Block.hpp"
#include <array>

// Mirrors BLOCK_TABLE from Block.hpp but owned by the game layer.
// Content registers definitions here at startup.
// Engine and mesher query this instead of the hardcoded table.
struct BlockDef {
    const char* name;
    uint8_t     atlasRow;
    uint8_t     atlasCol;
    bool        opaque;
};

class BlockRegistry {
public:
    static BlockRegistry& Get() {
        static BlockRegistry instance;
        return instance;
    }

    void Register(BlockType type, BlockDef def) {
        m_defs[static_cast<uint8_t>(type)] = def;
    }

    const BlockDef& Lookup(BlockType type) const {
        return m_defs[static_cast<uint8_t>(type)];
    }

private:
    std::array<BlockDef, 256> m_defs{};
};