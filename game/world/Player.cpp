#include "game/world/Player.hpp"

#include "engine/core/InputAction.hpp"
#include "engine/platform/InputManager.hpp"
#include "game/world/Block.hpp"
#include "game/world/ChunkManager.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

// Returns true if the AABB rooted at feetPos overlaps any solid block.
// A small inset epsilon prevents false positives when standing exactly
// on a block edge.
bool Player::Overlaps(glm::vec3 feetPos, const ChunkManager& world) const {
    constexpr float HW = WIDTH / 2.0f;
    constexpr float EPS = 0.001f;

    const int minX = static_cast<int>(std::floor(feetPos.x - HW + EPS));
    const int maxX = static_cast<int>(std::floor(feetPos.x + HW - EPS));
    const int minY = static_cast<int>(std::floor(feetPos.y + EPS));
    const int maxY = static_cast<int>(std::floor(feetPos.y + HEIGHT - EPS));
    const int minZ = static_cast<int>(std::floor(feetPos.z - HW + EPS));
    const int maxZ = static_cast<int>(std::floor(feetPos.z + HW - EPS));
    
    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z) {
                const BlockType b = world.GetBlock({ x, y, z });
                if (b != BlockType::Air && b != BlockType::Water)
                    return true;
            }
    return false;
}

void Player::Update(float dt, const InputManager& input,
                    const ChunkManager& world, float cameraYaw)
{
    // ── Horizontal wish direction from WASD + camera yaw ──────────────────
    const float yawR = glm::radians(cameraYaw);
    const glm::vec3 fwd   = {  std::cos(yawR), 0.f, std::sin(yawR) };
    const glm::vec3 right = { -std::sin(yawR), 0.f, std::cos(yawR) };

    glm::vec3 wish(0.f);
    if (input.IsActionDown(InputAction::MoveForward))  wish += fwd;
    if (input.IsActionDown(InputAction::MoveBackward)) wish -= fwd;
    if (input.IsActionDown(InputAction::MoveRight))    wish += right;
    if (input.IsActionDown(InputAction::MoveLeft))     wish -= right;

    const float wishLen = glm::length(wish);
    if (wishLen > 0.001f) wish /= wishLen;

    m_velocity.x = wish.x * MOVE_SPEED;
    m_velocity.z = wish.z * MOVE_SPEED;

    // ── Vertical: gravity + jump ───────────────────────────────────────────
    if (input.WasActionPressed(InputAction::Jump) && m_onGround) {
        m_velocity.y = JUMP_VEL;
        m_onGround   = false;
    }

    if (!m_onGround)
        m_velocity.y = std::max(m_velocity.y + GRAVITY * dt, TERMINAL_VEL);
    else if (m_velocity.y < 0.f)
        m_velocity.y = 0.f;

    // ── Integrate deltas ───────────────────────────────────────────────────
    const float dx = m_velocity.x * dt;
    const float dy = m_velocity.y * dt;
    const float dz = m_velocity.z * dt;

    // ── Move X ────────────────────────────────────────────────────────────
    bool blockedX = false;
    {
        glm::vec3 p = m_position;
        p.x += dx;
        if (Overlaps(p, world)) {
            blockedX    = true;
            m_velocity.x = 0.f;
        } else {
            m_position.x = p.x;
        }
    }

    // ── Move Z ────────────────────────────────────────────────────────────
    bool blockedZ = false;
    {
        glm::vec3 p = m_position;
        p.z += dz;
        if (Overlaps(p, world)) {
            blockedZ    = true;
            m_velocity.z = 0.f;
        } else {
            m_position.z = p.z;
        }
    }

    // ── Step-up: if horizontally blocked and on ground, try climbing ───────
    // Attempt to lift by STEP_HEIGHT and retry the blocked axis/axes.
    if ((blockedX || blockedZ) && m_onGround) {
        glm::vec3 elevated = m_position;
        elevated.y += STEP_HEIGHT;

        if (!Overlaps(elevated, world)) {
            bool any = false;

            if (blockedX) {
                glm::vec3 p = elevated;
                p.x += dx;
                if (!Overlaps(p, world)) {
                    elevated.x = p.x;
                    any = true;
                }
            }
            if (blockedZ) {
                glm::vec3 p = elevated;
                p.z += dz;
                if (!Overlaps(p, world)) {
                    elevated.z = p.z;
                    any = true;
                }
            }

            if (any) {
                m_position = elevated;
            }
        }
    }

    // ── Move Y (gravity / landing) ─────────────────────────────────────────
    {
        glm::vec3 p = m_position;
        p.y += dy;
        if (Overlaps(p, world)) {
            if (dy < 0.f) {
                // Snap feet to top of the block we hit below.
                m_position.y = std::floor(p.y) + 1.0f;
            } else {
                // Snap head to bottom of block above.
                m_position.y = std::floor(p.y + HEIGHT) - HEIGHT;
            }
            m_velocity.y = 0.f;
        } else {
            m_position.y = p.y;
        }
    }

    // ── Ground probe ───────────────────────────────────────────────────────
    // Probe 0.05 blocks below feet so m_onGround is always accurate regardless
    // of whether velocity.y was zero this frame (avoids the alternating-frame bug
    // where dy=0 never triggers the collision path above).
    {
        glm::vec3 probe = m_position;
        probe.y -= 0.05f;
        m_onGround = Overlaps(probe, world);
    }
}