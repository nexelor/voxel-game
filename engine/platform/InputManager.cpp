#include "InputManager.hpp"
 
#include <GLFW/glfw3.h>
 
InputManager::InputManager(GLFWwindow* window) : m_window(window) {}

bool InputManager::IsBindingDown(const KeyBindings::Binding& binding) const {
    if (binding.code < 0) return false;

    if (binding.type == KeyBindings::SourceType::Keyboard)
        return glfwGetKey(m_window, binding.code) == GLFW_PRESS;

    return glfwGetMouseButton(m_window, binding.code) == GLFW_PRESS;
}

void InputManager::Update() {
    // Shift current -> previous, then re-roll current.
    m_previousState = m_currentState;

    for (size_t i = 0; i < static_cast<size_t>(InputAction::Count); ++i) {
        const auto action = static_cast<InputAction>(i);
        m_currentState[i] = IsBindingDown(m_bindings.Get(action));
    }

    // Mouse delta
    double mx, my;
    glfwGetCursorPos(m_window, &mx, &my);

    if (m_firstMouseSample) {
        m_lastMouseX = mx;
        m_lastMouseY = my;
        m_firstMouseSample = false;
    }

    // x: right = positive. y: screen-down is positive in GLFW.
    // but camera-style consumers expect "up = positive" — flip here
    // so every consumer gets the same convention.
    m_mouseDelta.x = static_cast<float>(mx - m_lastMouseX);
    m_mouseDelta.y = static_cast<float>(m_lastMouseY - my);
 
    m_lastMouseX = mx;
    m_lastMouseY = my;
}

bool InputManager::IsActionDown(InputAction action) const {
    return m_currentState[static_cast<size_t>(action)];
}

bool InputManager::WasActionPressed(InputAction action) const {
    const size_t i = static_cast<size_t>(action);
    return m_currentState[i] && !m_previousState[i];
}

bool InputManager::WasActionReleased(InputAction action) const {
    const size_t i = static_cast<size_t>(action);
    return !m_currentState[i] && m_previousState[i];
}