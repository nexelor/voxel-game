#pragma once

#include <string>

struct GLFWwindow;

class Window {
public:
    Window(int width, int height, const std::string& title);

    ~Window();

    bool ShoudClose() const;

    void PollEvent();

    GLFWwindow* GetNativeWindow() const;

private:
    GLFWwindow* m_window = nullptr;
};