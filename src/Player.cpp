#include "Player.hpp"
#include "Camera.hpp"       // For Camera methods
#include "world/World.hpp"  // For World methods
#include "InputManager.hpp" // For InputManager methods
#include "util/Raycaster.hpp" // For VoxelRaycaster::castRay and RaycastResult
#include "block/Blocks.hpp"   // For Blocks::STONE_ID, Blocks::AIR_ID etc.
#include "InputManager.hpp"   // Assuming your KeyCode::MOUSE_BUTTON_LEFT etc. are here

#include <iostream> // For debugging

Player::Player(std::shared_ptr<Camera> camera, World& world, std::shared_ptr<InputManager> inputManager)
    : m_camera(camera), m_world(world), m_inputManager(inputManager), m_selectedBlockType(Blocks::STONE_ID) // Default to placing stone
{
    if (!m_camera) {
        throw std::runtime_error("Player requires a valid Camera pointer.");
    }
    if (!m_inputManager) {
        throw std::runtime_error("Player requires a valid InputManager pointer.");
    }
}

void Player::setSelectedBlockType(uint16_t type) {
    m_selectedBlockType = type;
}

void Player::update(float deltaTime) {
    // Update cooldowns
    if (m_breakCooldown > 0.0f) {
        m_breakCooldown -= deltaTime;
    }
    if (m_placeCooldown > 0.0f) {
        m_placeCooldown -= deltaTime;
    }

    handleBlockInteraction();
}

void Player::handleBlockInteraction() {
    if (!m_camera || !m_inputManager) return;

    // Block Breaking (e.g., Left Mouse Button)
    if (m_inputManager->isMouseButtonDown(KeyCode::MOUSE_BUTTON_LEFT) && m_breakCooldown <= 0.0f) {
        RaycastResult result = VoxelRaycaster::castRay(
            m_world,
            m_camera->getAbsoluteChunkPos(),
            m_camera->getLocalPositionInChunk(),
            m_camera->getFront()
        );

        if (result.hit) {
            // std::cout << "Ray hit block at: " << result.blockPosition.x << ", " << result.blockPosition.y << ", " << result.blockPosition.z << std::endl;
            m_world.setBlockID(result.blockPosition, Blocks::AIR_ID);
            m_breakCooldown = ACTION_COOLDOWN_TIME;
        }
    }

    // Block Placing (e.g., Right Mouse Button)
    if (m_inputManager->isMouseButtonDown(KeyCode::MOUSE_BUTTON_RIGHT) && m_placeCooldown <= 0.0f) {
        RaycastResult result = VoxelRaycaster::castRay(
            m_world,
            m_camera->getAbsoluteChunkPos(),
            m_camera->getLocalPositionInChunk(),
            m_camera->getFront()
        );

        if (result.hit) {
            glm::ivec3 placePosition = result.blockPosition + result.hitNormal;
            
            // Optional: Add a check here to prevent placing blocks inside the player
            // This would require the player to have its own bounding box and do an intersection test.
            // For now, we'll keep it simple.

            // std::cout << "Placing block at: " << placePosition.x << ", " << placePosition.y << ", " << placePosition.z << std::endl;
            m_world.setBlockID(placePosition, m_selectedBlockType);
            m_placeCooldown = ACTION_COOLDOWN_TIME;
        }
    }
}
