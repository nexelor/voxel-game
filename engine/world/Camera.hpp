#pragma once

#include "engine/renderer/Renderer.hpp"

#include <glm/glm.hpp>

struct GLFWwindow;

// ─────────────────────────────────────────────
//  Camera
//
//  First-person WASD + mouse-look camera.
//  Call Update() every frame with deltaTime;
//  it reads GLFW input and rebuilds the
//  CameraUBO ready for Renderer::UpdateCamera().
//
//  Mouse is captured on construction and
//  released when the window loses focus.
//  Press Escape to quit (sets window close flag).
// ─────────────────────────────────────────────

class Camera {
public:
    explicit Camera(GLFWwindow* window);

    // Call once per frame before Renderer::UpdateCamera
    void Update(float deltaTime);

    // Fills a CameraUBO for the renderer
    CameraUBO GetUBO(float aspectRatio) const;

    glm::vec3 GetPosition() const { return m_position; }

    // Exposed for application-level use
    float moveSpeed = 20.0f;   // blocks per second
    float mouseSensitivity = 0.1f;

private:
    GLFWwindow* m_window;

    glm::vec3 m_position { 16.0f, 40.0f, 16.0f }; // start above the chunk
    float m_yaw { -90.0f };  // looking toward -Z (north)
    float m_pitch {  -20.0f };

    double m_lastMouseX { 0.0 };
    double m_lastMouseY { 0.0 };
    bool m_firstMouse { true };

    glm::vec3 m_front { 0, 0, -1 };
    glm::vec3 m_right { 1, 0,  0 };
    glm::vec3 m_up { 0, 1,  0 };
};