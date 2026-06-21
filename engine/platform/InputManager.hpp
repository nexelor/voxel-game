#pragma once
 
#include "engine/core/InputAction.hpp"
#include "engine/platform/KeyBindings.hpp"
 
#include <array>
#include <glm/vec2.hpp>
 
struct GLFWwindow;
 
// ─────────────────────────────────────────────
//  InputManager
//
//  Polls GLFW once per frame and answers
//  semantic questions ("is MoveForward held?",
//  "was BreakBlock just pressed this frame?")
//  using whatever KeyBindings currently maps
//  each action to.
//
//  This replaces:
//    - the WASD/Space/Shift glfwGetKey() calls
//      that lived in Camera::Update
//    - the manual m_leftWasDown/m_rightWasDown
//      edge-detection that lived in
//      Application::HandleBlockInteraction
//    - the m_firstMouse / cursor-delta tracking
//      that lived in Camera
//
//  Usage (once per frame, before reading state):
//      inputManager.Update();
//
//      if (inputManager.IsActionDown(InputAction::MoveForward)) ...
//      if (inputManager.WasActionPressed(InputAction::BreakBlock)) ...
//      glm::vec2 look = inputManager.GetMouseDelta();
//
//  Rebinding (e.g. from a settings menu):
//      inputManager.GetBindings().Rebind(InputAction::FlyUp, GLFW_KEY_E);
// ─────────────────────────────────────────────

class InputManager {
public:
    explicit InputManager(GLFWwindow* window);

    // Call exactly once per frame, before any IsActionDown/
    // WasActionPressed/GetMouseDelta queries for that frame.
    void Update();
 
    // True every frame the action's bound input is held down.
    bool IsActionDown(InputAction action) const;
 
    // True only on the frame the action's bound input transitioned
    // from up -> down (handles GLFW_PRESS edge detection so callers
    // don't have to track "was it down last frame" themselves).
    bool WasActionPressed(InputAction action) const;
 
    // True only on the frame the action's bound input transitioned
    // from down -> up.
    bool WasActionReleased(InputAction action) const;
 
    // Mouse movement since the previous Update() call.
    // dx/dy are in raw screen pixels; x is inverted (right = positive),
    // y follows the convention "up = positive" (screen Y is flipped
    // for camera-style consumers).
    glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
 
    KeyBindings& GetBindings() { return m_bindings; }
    const KeyBindings& GetBindings() const { return m_bindings; }

private:
    bool IsBindingDown(const KeyBindings::Binding& binding) const;
 
    GLFWwindow* m_window = nullptr;
    KeyBindings m_bindings;
 
    std::array<bool, static_cast<size_t>(InputAction::Count)> m_currentState{};
    std::array<bool, static_cast<size_t>(InputAction::Count)> m_previousState{};
 
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_firstMouseSample = true;
    glm::vec2 m_mouseDelta{ 0.0f, 0.0f };
};