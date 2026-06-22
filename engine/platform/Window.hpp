#pragma once

#include <string>

struct GLFWwindow;

class Window {
public:
    Window(int width, int height, const std::string& title);

    ~Window();

    void PollEvent();

    bool ShouldClose() const;

    int GetWidth() const;
    int GetHeight() const;

    bool WasResized();
    GLFWwindow* GetNativeWindow() const;
    void ResetResizeFlag();

private:
    static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow* m_window = nullptr;

    int m_width;
    int m_height;

    bool m_framebufferResized = false;
};