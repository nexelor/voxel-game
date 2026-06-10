#include "Application.hpp"

Application::Application() : m_window(1280, 720, "Voxel Game") {

}

Application::~Application() = default;

void Application::Run() {
    while (m_running && !m_window.ShoudClose()) {
        m_window.PollEvent();

        Update();
        Render();
    }
}

void Application::Update() {

}

void Application::Render() {
    
}