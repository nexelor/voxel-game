#include "KeyBindings.hpp"
#include "engine/core/InputAction.hpp"
 
#include <GLFW/glfw3.h>
#include <cstddef>
 
KeyBindings::KeyBindings() : m_bindings(Defaults()) {}
 
// ─────────────────────────────────────────────
//  Defaults
//
//  This is the ONLY place raw GLFW_KEY_* /
//  GLFW_MOUSE_BUTTON_* constants should appear
//  for gameplay bindings. Change defaults here.
// ─────────────────────────────────────────────
 
std::array<KeyBindings::Binding, static_cast<size_t>(InputAction::Count)> KeyBindings::Defaults() {
    std::array<Binding, static_cast<size_t>(InputAction::Count)> defaults{};

    auto key = [](int code) { return Binding{ SourceType::Keyboard, code }; };
    auto mouse = [](int code) { return Binding{ SourceType::MouseButton, code }; };

    defaults[static_cast<size_t>(InputAction::MoveForward)] = key(GLFW_KEY_W);
    defaults[static_cast<size_t>(InputAction::MoveBackward)] = key(GLFW_KEY_S);
    defaults[static_cast<size_t>(InputAction::MoveLeft)] = key(GLFW_KEY_A);
    defaults[static_cast<size_t>(InputAction::MoveRight)] = key(GLFW_KEY_D);
    defaults[static_cast<size_t>(InputAction::Jump)] = key(GLFW_KEY_SPACE);
    defaults[static_cast<size_t>(InputAction::FlyDown)] = key(GLFW_KEY_LEFT_SHIFT);

    defaults[static_cast<size_t>(InputAction::BreakBlock)] = mouse(GLFW_MOUSE_BUTTON_LEFT);
    defaults[static_cast<size_t>(InputAction::PlaceBlock)] = mouse(GLFW_MOUSE_BUTTON_RIGHT);
    
    defaults[static_cast<size_t>(InputAction::OpenDebugMenu)] = key(GLFW_KEY_F3);

    defaults[static_cast<size_t>(InputAction::QuitGame)] = key(GLFW_KEY_ESCAPE);

    return defaults;
}

const KeyBindings::Binding& KeyBindings::Get(InputAction action) const {
    return m_bindings[static_cast<size_t>(action)];
}

void KeyBindings::Rebind(InputAction action, int glfwKey) {
    m_bindings[static_cast<size_t>(action)] = Binding{ SourceType::Keyboard, glfwKey };
}

void KeyBindings::RebindMouse(InputAction action, int glfwMouseButton) {
    m_bindings[static_cast<size_t>(action)] = Binding{ SourceType::MouseButton, glfwMouseButton };
}

void KeyBindings::ResetToDefault(InputAction action) {
    m_bindings[static_cast<size_t>(action)] = Defaults()[static_cast<size_t>(action)];
}

void KeyBindings::ResetAllToDefaults() {
    m_bindings = Defaults();
}