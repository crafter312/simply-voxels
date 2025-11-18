#include "Player.hpp"
#include "Camera.hpp"       // For Camera methods
#include "world/Chunk.hpp"  // For CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH
#include "world/World.hpp"  // For World methods
#include "InputManager.hpp" // For InputManager methods
#include "block/BlockRegistry.hpp" // Include for BlockRegistry
#include "block/Block.hpp"         // For Block class definition
#include "physics/BoundingBox.hpp" // For Physics::BlockAABB
#include "block/Blocks.hpp"   // For Blocks::COBBLESTONE_SLAB_ID, Blocks::AIR_ID etc.

#include <iostream> // For debugging
#include <glm/gtc/constants.hpp> // For glm::epsilon

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/compatibility.hpp> // For glm::lerp

Player::Player(
    std::shared_ptr<Camera> camera,
    World& world,
    BlockRegistry& blockRegistry,
    std::shared_ptr<InputManager> inputManager)
    : m_camera(camera), m_world(world), m_blockRegistry(blockRegistry), m_inputManager(inputManager),
      m_selectedBlockType(Blocks::COBBLESTONE_SLAB_ID), // Default to placing cobblestone slab
      m_absoluteChunkPos(0), // Initialize to a default, will be overwritten by setPosition
      m_localPositionInChunk(0.0f), // Initialize to a default, will be overwritten by setPosition
      m_velocity(0.0f),
      m_acceleration(0.0f),
      m_isGrounded(false), // Assume starting in air; physics/collision will correct this
      m_hasSpawned(false),   // Player has not been spawned initially
      m_moveSpeed(5.0f),
      m_sprintSpeedMultiplier(1.5f), // Matches Camera's SPRINT_MULTIPLIER if it was 2.5, adjust as needed
      m_wishHorizontalVelocity(0.0f),
      m_jumpForce(7.0f)
{
    if (!m_camera) {
        throw std::runtime_error("Player requires a valid Camera pointer.");
    }
    if (!m_inputManager) {
        throw std::runtime_error("Player requires a valid InputManager pointer.");
    }

    // Player position will be set externally via Player::setPosition.
    // Camera will be updated when setPosition is called.
}

void Player::setSelectedBlockType(uint16_t type) {
    m_selectedBlockType = type;
}

void Player::update(float deltaTime) {
    if (!m_hasSpawned) {
        return; // Do nothing if the player hasn't been spawned yet
    }

    // Handle input once per frame based on the full deltaTime
    handleMovementInput(deltaTime); // Sets m_wishHorizontalVelocity and handles jump impulse

    // Physics sub-stepping
    // Accumulator for time to be simulated this frame
    m_physicsTimeAccumulator += deltaTime;
    int substepsPerformed = 0;

    while (m_physicsTimeAccumulator >= FIXED_PHYSICS_DT && substepsPerformed < MAX_PHYSICS_SUBSTEPS) {
        // applyPhysics and resolveCollisionsAndMove now use the fixed delta time
        applyPhysics(FIXED_PHYSICS_DT);
        resolveCollisionsAndMove(FIXED_PHYSICS_DT);

        m_physicsTimeAccumulator -= FIXED_PHYSICS_DT;
        substepsPerformed++;
    }

    updateCameraPosition(); // Update camera after all physics steps are done
    
    // Update cooldowns
    if (m_breakCooldown > 0.0f) {
        m_breakCooldown -= deltaTime;
    }
    if (m_placeCooldown > 0.0f) {
        m_placeCooldown -= deltaTime;
    }

    handleBlockInteraction();
}

void Player::handleMovementInput(float deltaTime) {
    if (!m_inputManager || !m_camera) return;

    // Horizontal movement
    glm::vec3 camFront = m_camera->getFront();
    // True horizontal forward vector (ignoring camera pitch)
    glm::vec3 forward = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
    // True horizontal right vector
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f))); // World UP

    glm::vec3 wishDir(0.0f);
    if (m_inputManager->isKeyDown(KeyCode::W)) wishDir += forward;
    if (m_inputManager->isKeyDown(KeyCode::S)) wishDir -= forward;
    if (m_inputManager->isKeyDown(KeyCode::A)) wishDir -= right;
    if (m_inputManager->isKeyDown(KeyCode::D)) wishDir += right;

    float currentSpeed = m_moveSpeed;
    // Assuming LeftControl is the sprint key, as it was in Camera.cpp context
    if (m_inputManager->isKeyDown(KeyCode::LeftControl)) {
        currentSpeed *= m_sprintSpeedMultiplier;
    }

    if (glm::length(wishDir) > glm::epsilon<float>()) {
        wishDir = glm::normalize(wishDir);
    }

    // Set the desired horizontal velocity. applyPhysics will handle smoothing.
    m_wishHorizontalVelocity = wishDir * currentSpeed; // Y component will be ignored/zeroed by this

    // Jumping (direct impulse to current velocity)
    // Using isKeyDown for now. Ideally, InputManager would provide an `isKeyJustPressed`
    if (m_inputManager->isKeyDown(KeyCode::Space) && m_isGrounded) {
        m_velocity.y = m_jumpForce; // Apply an upward impulse
        m_isGrounded = false;       // Player is no longer on the ground
    }
}

void Player::applyPhysics(float deltaTime) {
    // Horizontal movement and drag are applied based on the m_isGrounded state
    // determined at the end of the PREVIOUS frame's resolveCollisionsAndMove for some effects (like stronger ground drag),
    // but acceleration towards wish velocity always applies.

    // Horizontal Momentum - Always apply to allow air control
    m_velocity.x = glm::lerp(m_velocity.x, m_wishHorizontalVelocity.x, HORIZONTAL_SMOOTHING_FACTOR * deltaTime);
    m_velocity.z = glm::lerp(m_velocity.z, m_wishHorizontalVelocity.z, HORIZONTAL_SMOOTHING_FACTOR * deltaTime); // .y of vec2 is our Z

    // Apply stronger horizontal drag if grounded and there's no horizontal input wish.
    if (glm::length(glm::vec2(m_wishHorizontalVelocity.x, m_wishHorizontalVelocity.z)) < glm::epsilon<float>()) {
        float dragMultiplier = 1.0f - glm::clamp(HORIZONTAL_DRAG_FACTOR * deltaTime, 0.0f, 1.0f);
        m_velocity.x *= dragMultiplier;
        m_velocity.z *= dragMultiplier;
    }

    // Determine if gravity should be applied based on the m_isGrounded state from the previous frame.
    bool applyDownwardGravity;
    if (m_isGrounded) { // If WAS grounded at the end of the last frame...
        if (m_velocity.y > glm::epsilon<float>()) { // ...and just jumped (velocity is upwards)...
            applyDownwardGravity = true; // ...gravity should act against the jump.
        } else { // ...and is just standing or walking (velocity.y is ~0 or negative from minor adjustments)...
            applyDownwardGravity = false; // ...no new downward acceleration from gravity.
            if (m_velocity.y < 0.0f) { // Correct any slight sinking if was grounded.
                m_velocity.y = 0.0f;
            }
        }
    } else { // If WAS in the air at the end of the last frame...
        applyDownwardGravity = true; // ...gravity continues to apply.
    }

    // Reset m_isGrounded for the CURRENT frame's collision detection.
    // It will be set to true again in resolveCollisionsAndMove if a Y-axis collision occurs.
    m_isGrounded = false;

    // Apply acceleration due to gravity (or lack thereof)
    if (applyDownwardGravity) {
        m_acceleration.y = -GRAVITY_ACCELERATION;
    } else {
        m_acceleration.y = 0.0f;
    }

    // Update vertical velocity from acceleration
    m_velocity.y += m_acceleration.y * deltaTime;

    // m_isGrounded will be set to true by Y-axis collision resolution if applicable.
}


namespace { // Anonymous namespace for helper functions local to this file
    bool checkAABBCollision(const Physics::BlockAABB& a, const Physics::BlockAABB& b) {
        return (a.minExtents.x < b.maxExtents.x && a.maxExtents.x > b.minExtents.x &&
                a.minExtents.y < b.maxExtents.y && a.maxExtents.y > b.minExtents.y &&
                a.minExtents.z < b.maxExtents.z && a.maxExtents.z > b.minExtents.z);
    }
}

void Player::normalizeAndCrossChunkBoundaryX() {
    int chunksMovedX = static_cast<int>(std::floor(m_localPositionInChunk.x / CHUNK_SIDE_LENGTH));
    if (chunksMovedX != 0) {
        m_absoluteChunkPos.x += chunksMovedX;
        m_localPositionInChunk.x -= chunksMovedX * CHUNK_SIDE_LENGTH;
    }
}

void Player::normalizeAndCrossChunkBoundaryY() {
    int chunksMovedY = static_cast<int>(std::floor(m_localPositionInChunk.y / CHUNK_SIDE_LENGTH));
    if (chunksMovedY != 0) {
        m_absoluteChunkPos.y += chunksMovedY;
        m_localPositionInChunk.y -= chunksMovedY * CHUNK_SIDE_LENGTH;
    }
}

void Player::normalizeAndCrossChunkBoundaryZ() {
    int chunksMovedZ = static_cast<int>(std::floor(m_localPositionInChunk.z / CHUNK_SIDE_LENGTH));
    if (chunksMovedZ != 0) {
        m_absoluteChunkPos.z += chunksMovedZ;
        m_localPositionInChunk.z -= chunksMovedZ * CHUNK_SIDE_LENGTH;
    }
}

void Player::resolveCollisionsAndMove(float deltaTime) {
    // --- X-AXIS MOVEMENT AND COLLISION ---
    m_localPositionInChunk.x += m_velocity.x * deltaTime;
    normalizeAndCrossChunkBoundaryX(); // Normalize before getting AABB for current position

    if (std::abs(m_velocity.x) > glm::epsilon<float>()) {
        Physics::BlockAABB playerWorldAABB = {getAABBMin(), getAABBMax()};
        std::vector<PotentialCollisionBlock> nearbyBlocks =
            m_world.getPotentialCollisionBlocks(m_absoluteChunkPos, m_localPositionInChunk, PLAYER_DIMENSIONS);

        for (const auto& blockInfo : nearbyBlocks) {
            const Block* blockDef = m_blockRegistry.getBlockDefinition(blockInfo.blockID);
            if (!blockDef) continue; // Safety check
            
            std::vector<Physics::BlockAABB> aabbsToTest;
            const auto& customShapeOpt = blockDef->getCustomShape();

            if (customShapeOpt && !customShapeOpt->aabbs.empty()) {
                aabbsToTest = customShapeOpt->aabbs;
            } else {
                // Block is not air (guaranteed by getPotentialCollisionBlocks)
                // and has no (or empty) custom shape, so treat as a full cube.
                aabbsToTest.push_back(Physics::BlockAABB(glm::vec3(0.0f), glm::vec3(1.0f)));
            }

            for (const auto& localBlockShapeAABB : aabbsToTest) {
                Physics::BlockAABB worldBlockAABB = {
                    glm::vec3(blockInfo.worldPosition) + localBlockShapeAABB.minExtents,
                    glm::vec3(blockInfo.worldPosition) + localBlockShapeAABB.maxExtents
                };

                if (checkAABBCollision(playerWorldAABB, worldBlockAABB)) {
                    if (m_velocity.x > 0) { // Moving right, collision with block's left face
                        // Calculate penetration
                        float penetration = playerWorldAABB.maxExtents.x - worldBlockAABB.minExtents.x;
                        // Adjust local position by penetration. Since player's AABB is centered,
                        // and m_localPositionInChunk.x is the center of the player's base X,
                        // moving the center back by `penetration` resolves it.
                        m_localPositionInChunk.x -= (penetration + COLLISION_RESOLUTION_BIAS);
                    } else { // Moving left, collision with block's right face
                        float penetration = worldBlockAABB.maxExtents.x - playerWorldAABB.minExtents.x;
                        m_localPositionInChunk.x += (penetration + COLLISION_RESOLUTION_BIAS);
                    }
                    m_velocity.x = 0.0f;
                    normalizeAndCrossChunkBoundaryX(); // Re-normalize after collision adjustment
                    playerWorldAABB = {getAABBMin(), getAABBMax()}; // Update player AABB for next potential check
                                                                  // (though we break, good practice if not breaking)
                    break; // Break from the inner aabbsToTest loop
                }
            }
            if (m_velocity.x == 0.0f) break; // Break from the outer nearbyBlocks loop if collision was resolved
        }
    }

    // --- Y-AXIS MOVEMENT AND COLLISION ---
    m_localPositionInChunk.y += m_velocity.y * deltaTime;
    normalizeAndCrossChunkBoundaryY(); // Normalize before getting AABB

    bool y_collision_resolved_this_frame = false;
    if (std::abs(m_velocity.y) > glm::epsilon<float>()) {
        Physics::BlockAABB playerWorldAABB = {getAABBMin(), getAABBMax()};
        std::vector<PotentialCollisionBlock> nearbyBlocks =
            m_world.getPotentialCollisionBlocks(m_absoluteChunkPos, m_localPositionInChunk, PLAYER_DIMENSIONS);

        for (const auto& blockInfo : nearbyBlocks) {
            const Block* blockDef = m_blockRegistry.getBlockDefinition(blockInfo.blockID);
            if (!blockDef) continue;
            
            std::vector<Physics::BlockAABB> aabbsToTest;
            const auto& customShapeOpt = blockDef->getCustomShape();

            if (customShapeOpt && !customShapeOpt->aabbs.empty()) {
                aabbsToTest = customShapeOpt->aabbs;
            } else {
                // Block is not air (guaranteed by getPotentialCollisionBlocks)
                // and has no (or empty) custom shape, so treat as a full cube.
                aabbsToTest.push_back(Physics::BlockAABB(glm::vec3(0.0f), glm::vec3(1.0f)));
            }

            for (const auto& localBlockShapeAABB : aabbsToTest) {
                Physics::BlockAABB worldBlockAABB = {
                    glm::vec3(blockInfo.worldPosition) + localBlockShapeAABB.minExtents,
                    glm::vec3(blockInfo.worldPosition) + localBlockShapeAABB.maxExtents
                };

                if (checkAABBCollision(playerWorldAABB, worldBlockAABB)) {
                    if (m_velocity.y > 0) { // Moving up
                        float penetration = playerWorldAABB.maxExtents.y - worldBlockAABB.minExtents.y;
                        m_localPositionInChunk.y -= (penetration + COLLISION_RESOLUTION_BIAS);
                    } else { // Moving down
                        float penetration = worldBlockAABB.maxExtents.y - playerWorldAABB.minExtents.y;
                        m_localPositionInChunk.y += (penetration + COLLISION_RESOLUTION_BIAS);
                        m_isGrounded = true; // Collided with something below
                    }
                    m_velocity.y = 0.0f;
                    y_collision_resolved_this_frame = true;
                    normalizeAndCrossChunkBoundaryY();
                    playerWorldAABB = {getAABBMin(), getAABBMax()};
                    break; // Break from the inner aabbsToTest loop
                }
            }
            if (y_collision_resolved_this_frame) break; // Break from the outer nearbyBlocks loop if collision was resolved
        }
    }
    // If player was moving downwards and no collision was resolved on Y, they are not grounded.
    // (m_isGrounded was reset in applyPhysics)
    // If m_velocity.y was 0 or positive, m_isGrounded remains false unless a collision happened.
    // No special logic needed here, m_isGrounded is correctly false if no downward collision occurred.

    // --- Z-AXIS MOVEMENT AND COLLISION ---
    m_localPositionInChunk.z += m_velocity.z * deltaTime;
    normalizeAndCrossChunkBoundaryZ(); // Normalize before getting AABB

    if (std::abs(m_velocity.z) > glm::epsilon<float>()) {
        Physics::BlockAABB playerWorldAABB = {getAABBMin(), getAABBMax()};
        std::vector<PotentialCollisionBlock> nearbyBlocks =
            m_world.getPotentialCollisionBlocks(m_absoluteChunkPos, m_localPositionInChunk, PLAYER_DIMENSIONS);

        for (const auto& blockInfo : nearbyBlocks) {
            const Block* blockDef = m_blockRegistry.getBlockDefinition(blockInfo.blockID);
            if (!blockDef) continue;
            
            std::vector<Physics::BlockAABB> aabbsToTest;
            const auto& customShapeOpt = blockDef->getCustomShape();

            if (customShapeOpt && !customShapeOpt->aabbs.empty()) {
                aabbsToTest = customShapeOpt->aabbs;
            } else {
                // Block is not air (guaranteed by getPotentialCollisionBlocks)
                // and has no (or empty) custom shape, so treat as a full cube.
                aabbsToTest.push_back(Physics::BlockAABB(glm::vec3(0.0f), glm::vec3(1.0f)));
            }

            for (const auto& localBlockShapeAABB : aabbsToTest) {
                Physics::BlockAABB worldBlockAABB = {
                    glm::vec3(blockInfo.worldPosition) + localBlockShapeAABB.minExtents,
                    glm::vec3(blockInfo.worldPosition) + localBlockShapeAABB.maxExtents
                };

                if (checkAABBCollision(playerWorldAABB, worldBlockAABB)) {
                    if (m_velocity.z > 0) { // Moving positive Z
                        float penetration = playerWorldAABB.maxExtents.z - worldBlockAABB.minExtents.z;
                        m_localPositionInChunk.z -= (penetration + COLLISION_RESOLUTION_BIAS);
                    } else { // Moving negative Z
                        float penetration = worldBlockAABB.maxExtents.z - playerWorldAABB.minExtents.z;
                        m_localPositionInChunk.z += (penetration + COLLISION_RESOLUTION_BIAS);
                    }
                    m_velocity.z = 0.0f;
                    normalizeAndCrossChunkBoundaryZ();
                    // playerWorldAABB = {getAABBMin(), getAABBMax()}; // Not strictly needed due to goto
                    break; // Break from the inner aabbsToTest loop
                }
            }
            if (m_velocity.z == 0.0f) break; // Break from the outer nearbyBlocks loop if collision was resolved
        }
    }
}

void Player::handleBlockInteraction() {
    if (!m_camera || !m_inputManager) {
        m_currentTargetedBlockInfo = std::nullopt; // Clear if no camera/input
        return;
    }
    // Perform raycast using the camera's chunk-relative position and orientation
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
            glm::i64vec3 placePosition_i64 = m_currentTargetedBlockInfo->blockPosition + glm::i64vec3(m_currentTargetedBlockInfo->hitNormal);
            glm::vec3 placePosition_f = glm::vec3(placePosition_i64);

            // Basic check: Don't place block inside player's AABB
            glm::vec3 playerMin = getAABBMin();
            glm::vec3 playerMax = getAABBMax();
            glm::vec3 blockMin = placePosition_f;
            glm::vec3 blockMax = placePosition_f + glm::vec3(1.0f);

            bool collisionWithPlayer = (playerMin.x < blockMax.x && playerMax.x > blockMin.x &&
                                        playerMin.y < blockMax.y && playerMax.y > blockMin.y &&
                                        playerMin.z < blockMax.z && playerMax.z > blockMin.z);
            if (!collisionWithPlayer) {
                m_world.setBlockID(placePosition_i64, m_selectedBlockType);
                m_placeCooldown = ACTION_COOLDOWN_TIME;
            }
        }
    }
}

const std::optional<RaycastResult>& Player::getCurrentTargetedBlockInfo() const {
    return m_currentTargetedBlockInfo;
}

void Player::updateCameraPosition() {
    if (m_camera) {
        // Calculate camera's target position based on player's chunk-relative position and eye height.
        glm::ivec3 cameraTargetChunkPos = m_absoluteChunkPos;
        glm::vec3 cameraTargetLocalPos = m_localPositionInChunk;

        // Add eye height to the player's local Y position.
        cameraTargetLocalPos.y += EYE_HEIGHT;

        // Normalize the camera's local Y position and adjust its chunk Y coordinate if necessary.
        // This handles cases where adding EYE_HEIGHT pushes the camera into an adjacent chunk vertically.
        int chunksMovedY = static_cast<int>(std::floor(cameraTargetLocalPos.y / CHUNK_SIDE_LENGTH));
        if (chunksMovedY != 0) { // Check if it actually crossed a boundary
            cameraTargetChunkPos.y += chunksMovedY;
            cameraTargetLocalPos.y -= chunksMovedY * CHUNK_SIDE_LENGTH;
        }
        // Note: We assume EYE_HEIGHT is less than CHUNK_SIDE_LENGTH, so it won't cross more than one chunk boundary.
        // If EYE_HEIGHT could be >= CHUNK_HEIGHT, a loop or more robust normalization might be needed,
        // but for typical player/camera setups, this is sufficient.
        m_camera->setPosition(cameraTargetChunkPos, cameraTargetLocalPos);
    }
}

// --- AABB Getter Methods ---
glm::vec3 Player::getAABBMin() const {
    // Reconstruct absolute world position for AABB calculation
    glm::vec3 playerBasePosition = glm::vec3(
        static_cast<float>(m_absoluteChunkPos.x * CHUNK_SIDE_LENGTH) + m_localPositionInChunk.x,
        static_cast<float>(m_absoluteChunkPos.y * CHUNK_SIDE_LENGTH) + m_localPositionInChunk.y,
        static_cast<float>(m_absoluteChunkPos.z * CHUNK_SIDE_LENGTH) + m_localPositionInChunk.z);
    return glm::vec3(
        playerBasePosition.x - PLAYER_DIMENSIONS.x / 2.0f,
        playerBasePosition.y, // Base of the player
        playerBasePosition.z - PLAYER_DIMENSIONS.z / 2.0f);
}

glm::vec3 Player::getAABBMax() const {
    // Reconstruct absolute world position for AABB calculation
    glm::vec3 playerBasePosition = glm::vec3(
        static_cast<float>(m_absoluteChunkPos.x * CHUNK_SIDE_LENGTH) + m_localPositionInChunk.x,
        static_cast<float>(m_absoluteChunkPos.y * CHUNK_SIDE_LENGTH) + m_localPositionInChunk.y,
        static_cast<float>(m_absoluteChunkPos.z * CHUNK_SIDE_LENGTH) + m_localPositionInChunk.z);
    return glm::vec3(
        playerBasePosition.x + PLAYER_DIMENSIONS.x / 2.0f,
        playerBasePosition.y + PLAYER_DIMENSIONS.y, // Top of the player
        playerBasePosition.z + PLAYER_DIMENSIONS.z / 2.0f);
}

glm::vec3 Player::getDimensions() { // Static method as per Player.hpp
    return PLAYER_DIMENSIONS;
}

void Player::setPosition(const glm::ivec3& absoluteChunkPos, const glm::vec3& localPositionInChunk) {
    m_absoluteChunkPos = absoluteChunkPos;
    m_localPositionInChunk = localPositionInChunk;

    if (!m_hasSpawned) {
        m_hasSpawned = true;
    }

    // Ensure the camera is updated to the new position
    updateCameraPosition();
}
