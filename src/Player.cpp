#include "Player.hpp"
#include "Camera.hpp"       // For Camera methods
#include "world/World.hpp"  // For World methods
#include "InputManager.hpp" // For InputManager methods
#include "block/BlockRegistry.hpp" // Include for BlockRegistry
#include "block/Blocks.hpp"   // For Blocks::STONE_ID, Blocks::AIR_ID etc.
#include "InputManager.hpp"   // Assuming your KeyCode::MOUSE_BUTTON_LEFT etc. are here

#include <iostream> // For debugging

Player::Player(
    std::shared_ptr<Camera> camera,
    World& world,
    BlockRegistry& blockRegistry,
    std::shared_ptr<InputManager> inputManager)
    : m_camera(camera), m_world(world), m_blockRegistry(blockRegistry), m_inputManager(inputManager), m_selectedBlockType(Blocks::COBBLESTONE_SLAB_ID) // Default to placing stone
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
    if (!m_camera || !m_inputManager) {
        m_currentTargetedBlockInfo = std::nullopt; // Clear if no camera/input
        return;
    }

    // Perform raycast once and store the result, regardless of mouse clicks
    m_currentTargetedBlockInfo = VoxelRaycaster::castRay(
        m_world,
        m_blockRegistry,
        m_camera->getAbsoluteChunkPos(),
        m_camera->getLocalPositionInChunk(),
        m_camera->getFront()
    );

    // Block Breaking (e.g., Left Mouse Button)
    if (m_inputManager->isMouseButtonDown(KeyCode::MOUSE_BUTTON_LEFT) && m_breakCooldown <= 0.0f) {
        if (m_currentTargetedBlockInfo && m_currentTargetedBlockInfo->hit) {
            m_world.setBlockID(m_currentTargetedBlockInfo->blockPosition, Blocks::AIR_ID);
            m_breakCooldown = ACTION_COOLDOWN_TIME;
        }
    }

    // Block Placing (e.g., Right Mouse Button)
    if (m_inputManager->isMouseButtonDown(KeyCode::MOUSE_BUTTON_RIGHT) && m_placeCooldown <= 0.0f) {
        if (m_currentTargetedBlockInfo && m_currentTargetedBlockInfo->hit) {
            // m_currentTargetedBlockInfo->blockPosition is glm::i64vec3
            // m_currentTargetedBlockInfo->hitNormal is glm::ivec3
            // The sum will be glm::i64vec3
            glm::i64vec3 placePosition = m_currentTargetedBlockInfo->blockPosition + glm::i64vec3(m_currentTargetedBlockInfo->hitNormal);
            
            // Optional: Add a check here to prevent placing blocks inside the player
            m_world.setBlockID(placePosition, m_selectedBlockType);
            m_placeCooldown = ACTION_COOLDOWN_TIME;
        }
    }
}

const std::optional<RaycastResult>& Player::getCurrentTargetedBlockInfo() const {
    return m_currentTargetedBlockInfo;
}
