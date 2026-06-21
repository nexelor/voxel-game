#pragma once
 
#include "engine/core/InputAction.hpp"
 
#include <array>
#include <cstddef>
 
// ─────────────────────────────────────────────
//  KeyBindings
//
//  Single source of truth mapping InputAction -> a
//  physical input (a GLFW key OR a GLFW mouse button).
//
//  This is intentionally separate from InputManager:
//  KeyBindings is just *data* (which key means what),
//  InputManager is the thing that *polls* GLFW using
//  that data. Splitting them means rebinding (or later,
//  loading bindings from a config file) never touches
//  any polling/edge-detection logic.
//
//  Usage:
//      KeyBindings bindings; // loads defaults
//      bindings.Rebind(InputAction::Jump, GLFW_KEY_E);
//      auto bound = bindings.Get(InputAction::Jump);
// ─────────────────────────────────────────────

class KeyBindings {
public:
    enum class SourceType : uint8_t {
        Keyboard,
        MouseButton
    };

    struct Binding {
        SourceType type = SourceType::Keyboard;
        int code = -1; // GLFW_KEY_* or GLFW_MOUSE_BUTTON_*
    };

    KeyBindings();

    // Look up the current binding for an action.
    // unbound actions return Binding{ .code = -1 }.
    const Binding& Get(InputAction action) const;
 
    // Rebind an action to a keyboard key at runtime.
    void Rebind(InputAction action, int glfwKey);
 
    // Rebind an action to a mouse button at runtime.
    void RebindMouse(InputAction action, int glfwMouseButton);
 
    // Reset a single action, or everything, back to defaults.
    void ResetToDefault(InputAction action);
    void ResetAllToDefaults();

private:
    static std::array<Binding, static_cast<size_t>(InputAction::Count)> Defaults();

    std::array<Binding, static_cast<size_t>(InputAction::Count)> m_bindings;
};