#pragma once

#include "game/world/Block.hpp"

#include <glm/glm.hpp>

// ─────────────────────────────────────────────
//  GameEvents
//
//  Plain data describing "something happened" in
//  the world. Publish these through EventBus
//  (engine/core/EventBus.hpp); keep all reactive
//  logic in the listeners, never in the struct
//  itself.
//
//  Adding a new event type:
//    1. Add a struct below.
//    2. EventBus::Get().Publish(YourEvent{...}) at
//       the call site that causes it.
//    3. EventBus::Get().Subscribe<YourEvent>(...)
//       wherever needs to react.
//  Nothing else in the engine needs to change.
// ─────────────────────────────────────────────

// Fired right after a block is removed (set to Air) from the world.
struct BlockBreakEvent {
    glm::ivec3 position;     // world-space coords of the broken block
    BlockType  brokenType;   // what the block WAS, just before breaking
};

// Fired right after a block is placed into the world.
struct BlockPlaceEvent {
    glm::ivec3 position;     // world-space coords of the new block
    BlockType  placedType;   // what was placed
};

// ── Add future events here, e.g.: ──
//
// struct ChunkLoadedEvent   { glm::ivec3 chunkCoord; };
// struct ChunkUnloadedEvent { glm::ivec3 chunkCoord; };
// struct PlayerDamagedEvent { float amount; float remainingHealth; };