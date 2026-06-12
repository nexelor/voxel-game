#pragma once

#include "engine/renderer/Renderer.hpp"

#include <glm/glm.hpp>

struct GLFWwindow;

// ─────────────────────────────────────────────
//  Camera — first-person fly camera
//
//  Call Update(dt) every frame; it reads GLFW
//  input and updates position + orientation.
//
//  GetFront() is used by Application to build
//  the ray for block pick/place raycasts.
//
//  Controls:
//    W/S/A/D        — move forward/back/left/right
//    Space          — fly up
//    Left Shift     — fly down
//    Mouse          — look
//    Escape         — close window
// ─────────────────────────────────────────────

class Camera {
public:
    explicit Camera(GLFWwindow* window);

    // Call once per frame before Renderer::UpdateCamera
    void Update(float deltaTime);

    // Fills a CameraUBO for the renderer
    CameraUBO GetUBO(float aspectRatio) const;

    glm::vec3 GetPosition() const { return m_position; }
    glm::vec3 GetFront() const { return m_front; }

    // Exposed for application-level use
    float moveSpeed = 20.0f;   // blocks per second
    float mouseSensitivity = 0.1f;

private:
    GLFWwindow* m_window;

    glm::vec3 m_position { 16.f, 132.f, 16.f };
    float m_yaw { -90.0f };  // looking toward -Z (north)
    float m_pitch {  -20.0f };

    double m_lastMouseX { 0.0 };
    double m_lastMouseY { 0.0 };
    bool m_firstMouse { true };

    glm::vec3 m_front { 0.f, 0.f, -1.f };
    glm::vec3 m_right { 1.f, 0.f,  0.f };
    glm::vec3 m_up { 0.f, 1.f,  0.f };
};