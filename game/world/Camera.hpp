#pragma once

#include "engine/renderer/Renderer.hpp"

#include <glm/glm.hpp>

struct GLFWwindow;
class InputManager;

// ─────────────────────────────────────────────
//  Camera — first-person fly camera
//
//  Call Update(dt, input) every frame; it reads
//  the InputManager's current action/mouse state
//  and updates position + orientation.
//
//  GetFront() is used by Application to build
//  the ray for block pick/place raycasts.
//
//  Controls are NOT hardcoded here — see
//  KeyBindings::Defaults() for the action -> key
//  mapping (MoveForward/Backward/Left/Right,
//  FlyUp, FlyDown, QuitGame).
// ─────────────────────────────────────────────
 
class Camera {
public:
    explicit Camera(GLFWwindow* window);

    void Update(float deltaTime, const InputManager& input);

    CameraUBO GetUBO(float aspectRatio) const;

    glm::vec3 GetPosition() const { return m_position; }
    glm::vec3 GetFront() const { return m_front; }

    float moveSpeed = 20.0f;   // blocks per second
    float mouseSensitivity = 0.1f;

private:
    GLFWwindow* m_window;

    glm::vec3 m_position { 16.f, 132.f, 16.f };
    float m_yaw { -90.0f };  // looking toward -Z (north)
    float m_pitch {  -20.0f };

    glm::vec3 m_front { 0.f, 0.f, -1.f };
    glm::vec3 m_right { 1.f, 0.f,  0.f };
    glm::vec3 m_up { 0.f, 1.f,  0.f };
};