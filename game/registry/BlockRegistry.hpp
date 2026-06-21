#pragma once
#include "game/world/Block.hpp"
#include "engine/renderer/TextureID.hpp"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>
 
// ─────────────────────────────────────────────
//  BlockDef
//
//  Content-layer description of a block. Each of the
//  6 faces (indexed the same way as the mesher's FaceDef
//  table: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z) gets its own
//  TextureID, so blocks like Grass can have a different
//  texture per face while Stone/Dirt just repeat one
//  TextureID across all 6.
//
//  TextureIDs are resolved to atlas tile coordinates once,
//  after TextureAtlas::Build() runs — see
//  BlockRegistry::ResolveAtlasCoords().
// ─────────────────────────────────────────────
 
struct BlockDef {
    const char* name = "";
    std::array<TextureID, 6> faceTextures{};
    bool opaque = false;
 
    // Resolved by ResolveAtlasCoords() after the atlas is built.
    // (col, row) per face, in atlas tile units.
    std::array<std::pair<uint32_t, uint32_t>, 6> faceAtlasCoords{};
};

// Convenience builders for the common cases, used by content registration code.
namespace BlockDefBuilder {
    // Every face uses the same texture (Stone, Dirt, Sand, ...)
    inline BlockDef Uniform(const char* name, const TextureID& tex, bool opaque) {
        BlockDef def;
        def.name = name;
        def.faceTextures.fill(tex);
        def.opaque = opaque;
        return def;
    }
 
    // Top / side / bottom distinct (Grass, ...).
    // Face order matches ChunkMesher::FACES: 0=+X 1=-X 2=+Y(top) 3=-Y(bottom) 4=+Z 5=-Z
    inline BlockDef TopSideBottom(const char* name, const TextureID& top,
        const TextureID& side, const TextureID& bottom, bool opaque)
    {
        BlockDef def;
        def.name = name;
        def.faceTextures = { side, side, top, bottom, side, side };
        def.opaque = opaque;
        return def;
    }
}

class BlockRegistry {
public:
    static BlockRegistry& Get() {
        static BlockRegistry instance;
        return instance;
    }
 
    void Register(BlockType type, BlockDef def) {
        m_defs[static_cast<uint8_t>(type)] = std::move(def);
    }
 
    const BlockDef& Lookup(BlockType type) const {
        return m_defs[static_cast<uint8_t>(type)];
    }

    // Returns every distinct TextureID referenced by any registered block.
    // Called once at startup so TextureAtlas knows exactly which PNGs it
    // needs to load/pack — adding a new block automatically pulls its
    // textures into the atlas with no separate registration step.
    std::vector<TextureID> CollectRequiredTextures() const {
        std::vector<TextureID> result;
        for (const auto& def : m_defs) {
            if (def.name == nullptr || def.name[0] == '\0') continue;
            for (const auto& tex : def.faceTextures) {
                if (tex.name.empty()) continue;
                bool dup = false;
                for (const auto& existing : result) {
                    if (existing == tex) { dup = true; break; }
                }
                if (!dup) result.push_back(tex);
            }
        }
        return result;
    }

    // Called once after TextureAtlas::Build(). Looks up the atlas tile
    // coordinate for every face's TextureID and caches it directly on
    // the BlockDef so the mesher does zero string/hash lookups per-face
    // during chunk meshing (it was already doing a flat array index
    // before this change — this preserves that performance property).
    template <typename AtlasCoordLookupFn>
    void ResolveAtlasCoords(AtlasCoordLookupFn&& lookup) {
        for (auto& def : m_defs) {
            if (def.name == nullptr || def.name[0] == '\0') continue;
            for (int face = 0; face < 6; ++face) {
                def.faceAtlasCoords[face] = lookup(def.faceTextures[face]);
            }
        }
    }
 
private:
    std::array<BlockDef, 256> m_defs{};
};