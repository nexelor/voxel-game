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
    reg.Register(BlockType::Air, BlockDef{ "air", {}, RenderLayer::Opaque, false });

    reg.Register(BlockType::Stone, Uniform(
        "stone", TextureID("minecraft:stone")));

    reg.Register(BlockType::Dirt, Uniform(
        "dirt", TextureID("minecraft:dirt")));

    reg.Register(BlockType::Grass, TopSideBottom(
        "grass",
        TextureID("minecraft:grass_top"),
        TextureID("minecraft:grass_side"),
        TextureID("minecraft:dirt")));

    reg.Register(BlockType::Sand, Uniform(
        "sand", TextureID("minecraft:sand")));

    // ── Transparent / translucent blocks ──

    // Glass: full cube, smoothly alpha-blended. Still occludes
    // neighbors — a stone block behind glass doesn't render its
    // hidden face, exactly like vanilla.
    reg.Register(BlockType::Glass, Uniform(
        "glass", TextureID("minecraft:glass"), RenderLayer::Translucent));

    // Leaves: full cube, binary alpha-tested (no soft blending, no
    // sort dependency — cheaper than Translucent).
    reg.Register(BlockType::Leaves, Uniform(
        "leaves_oak", TextureID("minecraft:leaves_oak"), RenderLayer::Cutout));

    // Water: full cube for now (a partial-height water mesh is a
    // good next step once this lands — see the comment in
    // ChunkMesh.cpp), smoothly blended.
    // reg.Register(BlockType::Water, Uniform(
    //     "water", TextureID("minecraft:water"), RenderLayer::Translucent));
}