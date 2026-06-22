#include "game/world/Camera.hpp"
#include "engine/platform/InputManager.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

Camera::Camera(GLFWwindow* window) : m_window(window) {
    // Capture the mouse cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    // Raw motion removes OS acceleration — smoother for games
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

void Camera::Update(float deltaTime, const InputManager& input) {
    (void) deltaTime;

    const glm::vec2 mouseDelta = input.GetMouseDelta();

    m_yaw += mouseDelta.x * mouseSensitivity;
    m_pitch += mouseDelta.y * mouseSensitivity;
    m_pitch = std::clamp(m_pitch, -89.f, 89.f);

    const float yawR = glm::radians(m_yaw);
    const float pitchR = glm::radians(m_pitch);

    m_front = glm::normalize(glm::vec3{
        std::cos(yawR) * std::cos(pitchR),
        std::sin(pitchR),
        std::sin(yawR) * std::cos(pitchR)
    });
    m_right = glm::normalize(glm::cross(m_front, glm::vec3{ 0.f, 1.f, 0.f }));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}

CameraUBO Camera::GetUBO(float aspectRatio) const {
    CameraUBO ubo{};
    ubo.view = glm::lookAt(m_position, m_position + m_front, m_up);
    ubo.proj = glm::perspective(glm::radians(70.0f), aspectRatio, 0.1f, 1000.f);
    // Vulkan clip-space has Y flipped relative to OpenGL
    ubo.proj[1][1] *= -1.f;
    ubo.cameraPos = m_position;
    return ubo;
}