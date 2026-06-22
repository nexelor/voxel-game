#pragma once

#include <glm/glm.hpp>

class InputManager;
class ChunkManager;

class Player {
public:
    Player() = default;

    void Update(float dt, const InputManager& input, const ChunkManager& world, float cameraYaw);

    glm::vec3 GetPosition() const { return m_position; }
    glm::vec3 GetEyePosition() const { return m_position + glm::vec3(0.f, EYE_OFFSET, 0.f); }
    void SetPosition(glm::vec3 p) { m_position = p; }

    static constexpr float WIDTH = 0.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE_OFFSET = 1.62f;

private:
    bool Overlaps(glm::vec3 feetPos, const ChunkManager& world) const;

    static constexpr float GRAVITY = -28.0f;    
    static constexpr float JUMP_VEL = 9.0f;
    static constexpr float MOVE_SPEED = 4.3f;
    static constexpr float STEP_HEIGHT = 1.0f;
    static constexpr float TERMINAL_VEL = -50.0f;

    glm::vec3 m_position { 16.f, 400.f, 16.f }; // For now the player gets spawn way to high, will be changed later
    glm::vec3 m_velocity { 0.f };
    bool m_onGround = false;
};