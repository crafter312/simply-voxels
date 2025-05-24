#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "util/Raycaster.hpp" // For VoxelRaycaster::castRay and RaycastResult

#include <memory> // For std::shared_ptr
#include <glm/glm.hpp>
#include <optional> // For std::optional

// Forward declarations
class Camera;
class World;
class InputManager;
class BlockRegistry; // Forward declare BlockRegistry

class Player {
public:
    Player(
        std::shared_ptr<Camera> camera,
        World& world,
        BlockRegistry& blockRegistry, // Add BlockRegistry reference
        std::shared_ptr<InputManager> inputManager);

    void update(float deltaTime);
    const std::optional<RaycastResult>& getCurrentTargetedBlockInfo() const;
    void setSelectedBlockType(uint16_t type);

    // --- Physics and Position ---
    glm::ivec3 getAbsoluteChunkPos() const { return m_absoluteChunkPos; }
    glm::vec3 getLocalPositionInChunk() const { return m_localPositionInChunk; }
    glm::vec3 getVelocity() const { return m_velocity; }
    bool isGrounded() const { return m_isGrounded; }
    bool hasSpawned() const { return m_hasSpawned; }

    // Bounding Box related
    // For simplicity, assuming m_position is at the player's feet, center horizontally.
    // A more robust AABB class/struct might be beneficial later.
    glm::vec3 getAABBMin() const;
    glm::vec3 getAABBMax() const;
    static glm::vec3 getDimensions();
    void setPosition(const glm::ivec3& absoluteChunkPos, const glm::vec3& localPositionInChunk);

private:
    void handleBlockInteraction();
    void handleMovementInput(float deltaTime);
    void applyPhysics(float deltaTime); // Will handle gravity, etc.
    // void updatePlayerPosition(float deltaTime); // Replaced by resolveCollisionsAndMove
    void resolveCollisionsAndMove(float deltaTime); // Handles movement, collision, and chunk boundary crossing
    void normalizeAndCrossChunkBoundaryX();
    void normalizeAndCrossChunkBoundaryY();
    void normalizeAndCrossChunkBoundaryZ();
    void updateCameraPosition();

    std::shared_ptr<Camera> m_camera;
    World& m_world; // Reference, as Player doesn't own the World
    BlockRegistry& m_blockRegistry; // Reference to the block registry
    std::shared_ptr<InputManager> m_inputManager;

    uint16_t m_selectedBlockType; // The type of block to place

    // Cooldowns to prevent breaking/placing too fast
    std::optional<RaycastResult> m_currentTargetedBlockInfo; // Optional to indicate no hit
    float m_breakCooldown = 0.0f;
    float m_placeCooldown = 0.0f;
    const float ACTION_COOLDOWN_TIME = 0.2f; // e.g., 5 actions per second

    // --- Physics State ---
    // Player's position is now stored as chunk coordinates + local offset
    glm::ivec3 m_absoluteChunkPos;      // Player's absolute chunk coordinate
    glm::vec3 m_localPositionInChunk; // Player's position within their current chunk [0, CHUNK_DIMENSION)
    glm::vec3 m_velocity;
    glm::vec3 m_acceleration;
    bool m_isGrounded;
    bool m_hasSpawned; // Flag to indicate if the player has been spawned/positioned in the world

    // --- Player Characteristics ---
    static const glm::vec3 PLAYER_DIMENSIONS; // Width, Height, Depth
    static const float EYE_HEIGHT;            // Y-offset from m_position to camera

    // --- Movement Parameters ---
    float m_moveSpeed;
    float m_sprintSpeedMultiplier;
    float m_jumpForce;
};

#endif // PLAYER_HPP
