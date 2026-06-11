#include "Window.hpp"

#include "engine/core/Assert.hpp"
#include "engine/core/Logger.hpp"

#include <GLFW/glfw3.h>

Window::Window(int width, int height, const std::string& title) : m_width(width), m_height(height) {
    VG_ASSERT(glfwInit(), "Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    VG_ASSERT(m_window, "Failed to create window");
        
    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);

    Logger::Log(LogLevel::Info, "Window", "Window created");
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();

    Logger::Log(LogLevel::Info, "Window", "Window destroyed");
}

void Window::PollEvent() {
    glfwPollEvents();
}

bool Window::ShoudClose() const {
    return glfwWindowShouldClose(m_window);
}

int Window::GetWidth() const { return m_width; }
int Window::GetHeight() const { return m_height; }
bool Window::WasResized() { return m_framebufferResized; }
void Window::ResetResizeFlag() { m_framebufferResized = false; }
GLFWwindow* Window::GetNativeWindow() const { return m_window; }

void Window::FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));

    app->m_width = width;
    app->m_height = height;
    app->m_framebufferResized = true;
}