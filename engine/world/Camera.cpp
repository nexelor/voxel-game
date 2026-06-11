#include "Camera.hpp"

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

///
/// Update
///

void Camera::Update(float deltaTime) {
    // Mouse look
    double mx, my;
    glfwGetCursorPos(m_window, &mx, &my);

    if (m_firstMouse) {
        m_lastMouseX = mx;
        m_lastMouseY = my;
        m_firstMouse = false;
    }

    double dx = mx - m_lastMouseX;
    double dy = m_lastMouseY - my;  // y is inverted in screen space
    m_lastMouseX = mx;
    m_lastMouseY = my;

    m_yaw += static_cast<float>(dx) * mouseSensitivity;
    m_pitch += static_cast<float>(dy) * mouseSensitivity;
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

    // Recompute direction vectors
    float yawR = glm::radians(m_yaw);
    float pitchR = glm::radians(m_pitch);

    m_front = glm::normalize(glm::vec3{
        std::cos(yawR) * std::cos(pitchR),
        std::sin(pitchR),
        std::sin(yawR) * std::cos(pitchR)
    });
    m_right = glm::normalize(glm::cross(m_front, glm::vec3{ 0, 1, 0 }));
    m_up = glm::normalize(glm::cross(m_right, m_front));

    // WASD movement
    const float speed = moveSpeed * deltaTime;

    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) m_position += m_front * speed;
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) m_position -= m_front * speed;
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) m_position -= m_right * speed;
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) m_position += m_right * speed;
    if (glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS) m_position += glm::vec3{0,1,0} * speed;
    if (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) m_position -= glm::vec3{0,1,0} * speed;

    // Escape → close window
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

///
/// UBO
///

CameraUBO Camera::GetUBO(float aspectRatio) const {
    CameraUBO ubo{};
    ubo.view = glm::lookAt(m_position, m_position + m_front, m_up);
    ubo.proj = glm::perspective(glm::radians(70.0f), aspectRatio, 0.1f, 1000.0f);
    // Vulkan clip-space has Y flipped relative to OpenGL
    ubo.proj[1][1] *= -1.0f;
    ubo.cameraPos  = m_position;
    return ubo;
}