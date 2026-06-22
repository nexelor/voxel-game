#pragma once

#include "engine/renderer/Renderer.hpp"

#include <glm/glm.hpp>

struct GLFWwindow;
class InputManager;

// ─────────────────────────────────────────────
//  Camera — first-person view (mouse look only)
//
//  Position is set externally by the Player each
//  frame via SetPosition(player.GetEyePosition()).
//  Update(dt, input) only handles mouse look.
//
//  GetFront() / GetYaw() are used by Application
//  (block raycasts) and Player (WASD orientation).
// ─────────────────────────────────────────────

class Camera {
public:
    explicit Camera(GLFWwindow* window);

    void Update(float deltaTime, const InputManager& input);

    CameraUBO GetUBO(float aspectRatio) const;

    void SetPosition(glm::vec3 p) { m_position = p; }

    glm::vec3 GetPosition() const { return m_position; }
    glm::vec3 GetFront() const { return m_front; }
    float GetYaw() const { return m_yaw; }

    float mouseSensitivity = 0.1f;

private:
    GLFWwindow* m_window;

    glm::vec3 m_position { 16.f, 130.6f, 16.f };
    float m_yaw { -90.0f };
    float m_pitch {  -20.0f };

    glm::vec3 m_front { 0.f, 0.f, -1.f };
    glm::vec3 m_right { 1.f, 0.f,  0.f };
    glm::vec3 m_up { 0.f, 1.f,  0.f };
};