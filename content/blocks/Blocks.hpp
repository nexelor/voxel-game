#pragma once
#include "game/registry/BlockRegistry.hpp"
 
// ─────────────────────────────────────────────
//  RegisterAllBlocks
//
//  This is the single place content adds new blocks.
//  Adding a block is two steps:
//    1. Drop the texture PNG(s) under
//       content/assets/textures/<namespace>/<name>.png
//    2. Add one Register() call below.
//
//  No atlas bookkeeping needed — TextureAtlas scans
//  every TextureID referenced here at startup, packs
//  whatever it finds, and BlockRegistry resolves the
//  coordinates automatically (see Application::Initialize).
// ─────────────────────────────────────────────

inline void RegisterAllBlocks(BlockRegistry& reg) {
    using namespace BlockDefBuilder;

    // Air has no faces/texture — left as a default-constructed (empty) BlockDef.
    reg.Register(BlockType::Air, BlockDef{ "air", {}, false });

    reg.Register(BlockType::Stone, Uniform(
        "stone", TextureID("minecraft:stone"), true));

    reg.Register(BlockType::Dirt, Uniform(
        "dirt", TextureID("minecraft:dirt"), true));

    reg.Register(BlockType::Grass, TopSideBottom(
        "grass",
        TextureID("minecraft:grass_top"),
        TextureID("minecraft:grass_side"),
        TextureID("minecraft:dirt"),     // grass bottom reuses the dirt texture
        true));

    reg.Register(BlockType::Sand, Uniform(
        "sand", TextureID("minecraft:sand"), true));
}