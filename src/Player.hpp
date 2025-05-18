#ifndef PLAYER_HPP
#define PLAYER_HPP

#include <memory> // For std::shared_ptr
#include <glm/glm.hpp>

// Forward declarations
class Camera;
class World;
class InputManager;

class Player {
public:
    Player(std::shared_ptr<Camera> camera, World& world, std::shared_ptr<InputManager> inputManager);

    void update(float deltaTime);

    // Potentially add methods to change selected block, etc.
    void setSelectedBlockType(uint16_t type);

private:
    void handleBlockInteraction();

    std::shared_ptr<Camera> m_camera;
    World& m_world; // Reference, as Player doesn't own the World
    std::shared_ptr<InputManager> m_inputManager;

    uint16_t m_selectedBlockType; // The type of block to place

    // Cooldowns to prevent breaking/placing too fast
    float m_breakCooldown = 0.0f;
    float m_placeCooldown = 0.0f;
    const float ACTION_COOLDOWN_TIME = 0.2f; // e.g., 5 actions per second
};

#endif // PLAYER_HPP
