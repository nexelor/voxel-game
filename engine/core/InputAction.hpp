#pragma once
 
#include <cstdint>
 
// ─────────────────────────────────────────────
//  InputAction
//
//  Semantic actions the game cares about. Game
//  and engine code should NEVER reference a raw
//  GLFW_KEY_* / GLFW_MOUSE_BUTTON_* constant —
//  always go through InputAction + InputManager.
//
//  Adding a new bindable action:
//    1. Add an entry here (before Count).
//    2. Add its default binding in
//       KeyBindings::Defaults() (KeyBindings.cpp).
//  That's it — InputManager and KeyBindings both
//  iterate generically over this enum.
// ─────────────────────────────────────────────

enum class InputAction : uint8_t {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    FlyDown,

    BreakBlock,
    PlaceBlock,

    OpenDebugMenu,

    QuitGame,

    Count
};