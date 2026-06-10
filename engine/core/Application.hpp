#pragma once

#include "engine/platform/Window.hpp"

class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    Window m_window;

    bool m_running = true;

    void Update();
    void Render();
};