#pragma once

#include "game/world/Block.hpp"
#include "engine/renderer/TextureID.hpp"
#include <array>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────
//  RenderLayer
//
//  Which draw pass (and pipeline) a block's faces
//  go into. The mesher sorts every face into one
//  of two GPU buffers per chunk based on this:
//
//    Opaque / Cutout  → "solid" buffer  → m_voxelPipeline
//                        (depth write ON, no blending —
//                        Cutout just relies on the frag
//                        shader's existing alpha-discard,
//                        e.g. leaves)
//    Translucent      → "translucent" buffer → m_translucentPipeline
//                        (depth write OFF, blending ON,
//                        drawn after every solid chunk,
//                        e.g. glass/water)
//
//  Opaque and Cutout share a pipeline because the only
//  difference between them is a shader-side alpha
//  discard that already runs unconditionally — there's
//  no GPU fixed-function state that needs to differ.
// ─────────────────────────────────────────────

enum class RenderLayer : uint8_t {
    Opaque,
    Cutout,
    Translucent,
};

struct BlockDef {
    const char* name = "";
    std::array<TextureID, 6> faceTextures{};
    RenderLayer renderLayer = RenderLayer::Opaque;

    // Does this block occupy the full block volume and therefore
    // hide any neighbor face that touches it, regardless of how
    // transparent it LOOKS? True for every block right now (incl.
    // Glass/Leaves — a glass cube still fully blocks the stone face
    // behind it, exactly like vanilla). Only Air sets this false.
    // A future non-cube block (slabs, crops, a half-height liquid)
    // would also set this false — this flag is that extension point.
    bool occludes = true;

    // Resolved by ResolveAtlasCoords() after the atlas is built.
    std::array<std::pair<uint32_t, uint32_t>, 6> faceAtlasCoords{};
};

// Convenience builders for the common cases, used by content registration code.
namespace BlockDefBuilder {
    // Every face uses the same texture (Stone, Dirt, Sand, ...)
    inline BlockDef Uniform(const char* name, const TextureID& tex,
        RenderLayer layer = RenderLayer::Opaque, bool occludes = true)
    {
        BlockDef def;
        def.name = name;
        def.faceTextures.fill(tex);
        def.renderLayer = layer;
        def.occludes = occludes;
        return def;
    }
 
    // Top / side / bottom distinct (Grass, ...).
    inline BlockDef TopSideBottom(const char* name, const TextureID& top,
        const TextureID& side, const TextureID& bottom,
        RenderLayer layer = RenderLayer::Opaque, bool occludes = true)
    {
        BlockDef def;
        def.name = name;
        def.faceTextures = { side, side, top, bottom, side, side };
        def.renderLayer = layer;
        def.occludes = occludes;
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

    std::vector<TextureID> CollectRequiredTextures() const {
        std::unordered_set<TextureID, TextureIDHash> seen;
        std::vector<TextureID> result;
        for (const auto& def : m_defs) {
            if (def.name == nullptr || def.name[0] == '\0') continue;
            for (const auto& tex : def.faceTextures) {
                if (tex.name.empty()) continue;
                if (seen.insert(tex).second)
                    result.push_back(tex);
            }
        }
        return result;
    }

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

// ─────────────────────────────────────────────
//  Mesher-facing helpers
//
//  These are the two questions ChunkMesher actually
//  asks. Kept here (not in Block.hpp) since both need
//  registry data.
// ─────────────────────────────────────────────

// True if `neighbor` fully hides any face of `current` touching it.
//   1. neighbor is Air                → never
//   2. neighbor's layer is Opaque     → always (a solid block fully
//                                        blocks any face behind it)
//   3. neighbor == current block type → cull the shared internal
//                                        face (stops double-blended
//                                        seams between two glass/
//                                        water/leaf blocks — same
//                                        trick vanilla Minecraft uses)
//   Anything else (two DIFFERENT translucent/cutout blocks touching,
//   e.g. glass against leaves) renders both faces — correct, since
//   two different see-through materials should both be visible.
inline bool FaceHidden(BlockType current, BlockType neighbor) {
    if (neighbor == BlockType::Air) return false;

    const BlockDef& neighborDef = BlockRegistry::Get().Lookup(neighbor);
    if (neighborDef.renderLayer == RenderLayer::Opaque) return true;

    return neighbor == current;
}

// Whether `neighbor` should darken a corner for ambient occlusion.
// AO is meant to shade corners against solid geometry — letting
// glass/leaves/water contribute would shade a corner next to a
// window or a tree as if it were stone. Only Opaque blocks count.
inline bool ContributesToAO(BlockType neighbor) {
    return BlockRegistry::Get().Lookup(neighbor).renderLayer == RenderLayer::Opaque;
}