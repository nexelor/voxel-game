#include "Timer.hpp"

Timer::Timer() {
    m_startTime = Clock::now();
    m_lastFrameTime = m_startTime;
}

void Timer::Update() {
    const auto currentTime = Clock::now();

    m_deltaTime = std::chrono::duration<float>(
        currentTime - m_startTime
    ).count();

    m_elapsedTime = std::chrono::duration<float>(
        currentTime - m_startTime
    ).count();

    m_lastFrameTime = currentTime;
}

float Timer::GetDeltaTime() const {
    return m_deltaTime;
}

float Timer::GetElapsedTime() const {
    return m_elapsedTime;
}