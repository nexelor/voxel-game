#include "Window.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>

Window::Window(int width, int height, const std::string& title) {
    glfwInit();

    glfwInitHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!m_window) {
        throw std::runtime_error("Failed to create window");
    }
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::ShoudClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::PollEvent() {
    glfwPollEvents();
}

GLFWwindow* Window::GetNativeWindow() const {
    return m_window;
}