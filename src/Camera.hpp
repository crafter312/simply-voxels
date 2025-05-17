#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>    // Required for std::shared_ptr
#include <algorithm> // For std::max

// Include for constants needed for FAR_PLANE_DISTANCE calculation
#include "world/Chunk.hpp" // For CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH
#include "world/World.hpp"   // For World::UNLOAD_CHUNK_RADIUS

// Forward declaration for InputManager
class InputManager;

class Camera {
public:
    // Constructor now accepts a shared_ptr to InputManager
    Camera(std::shared_ptr<InputManager> inputManager);
    ~Camera();

    void update(float deltaTime);

    // Get the camera's position relative to the provided rebase origin for rendering
    glm::vec3 getPositionRelativeTo(glm::ivec3 rebaseOriginChunkCoord) const;
    glm::mat4 getViewMatrix(glm::ivec3 rebaseOriginChunkCoord) const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // Getters
    glm::ivec3 getAbsoluteChunkPos() const { return m_absoluteChunkPos; }
    glm::vec3 getLocalPositionInChunk() const { return m_localPositionInChunk; }
    float getYaw() const;
    float getFov() const;
    glm::vec3 getFront() const;
    float getPitch() const;
    // Setters
    void setPosition(const glm::vec3& position);
    void setYaw(float yaw);
    void setPitch(float pitch);
    void setFov(float fov);

    // For origin rebasing
    // These methods are no longer needed with the new absolute position tracking
    // glm::vec3 getAbsolutePosition(glm::ivec3 currentRebaseOriginChunkCoord, glm::ivec3 chunkDimensions) const;
    // void rebase(glm::ivec3 newRebaseOriginChunkCoord, glm::ivec3 oldRebaseOriginChunkCoord, glm::ivec3 chunkDimensions);

private:
    std::shared_ptr<InputManager> m_inputManager; // Store the InputManager

    // Camera Attributes
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    // Euler Angles
    float m_yaw;   // In degrees
    float m_pitch; // In degrees

    // Camera options
    float m_fov; // In degrees
    glm::ivec3 m_absoluteChunkPos; // Camera's absolute chunk coordinate
    glm::vec3 m_localPositionInChunk; // Camera's position within the current chunk [0, CHUNK_DIMENSION)
    float m_mouseSensitivity;     // Sensitivity for mouse look
    glm::vec3 m_horizontalVelocity; // For W, A, S, D momentum
    float m_verticalVelocity;     // For Space, Left Shift momentum

    void updateCameraVectors();

public: // Making constants public for potential external reference, or move to private if only internal
    static const float BASE_MOVE_SPEED;
    static const float SPRINT_MULTIPLIER;

    // Constants for projection matrix
    static constexpr float NEAR_PLANE_DISTANCE = 0.1f;
    static constexpr float SQRT3_APPROX = 1.73205081f; // sqrt(3)

    // Calculate the maximum dimension of a chunk (assuming CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH are available from Chunk.hpp)
    // std::max with an initializer_list is constexpr since C++14
    static constexpr float MAX_CHUNK_DIM = static_cast<float>(
        std::max({CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH})
    );

    // Calculate far plane distance to encompass the furthest unloadable chunk diagonally
    // For spherical loading, this is roughly (Radius in Chunks + Diagonal of one Chunk) * Chunk Dimension
    static constexpr float FAR_PLANE_DISTANCE =
        (static_cast<float>(UNLOAD_CHUNK_RADIUS) + SQRT3_APPROX) * MAX_CHUNK_DIM;
};

#endif // CAMERA_HPP