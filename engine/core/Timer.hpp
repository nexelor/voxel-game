#pragma once

#include <chrono>

class Timer {
public:
    Timer();

    void Update();

    float GetDeltaTime() const;
    float GetElapsedTime() const;

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_startTime;
    Clock::time_point m_lastFrameTime;

    float m_deltaTime = 0.0f;
    float m_elapsedTime = 0.0f;
};