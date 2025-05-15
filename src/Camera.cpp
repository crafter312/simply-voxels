#include "Camera.hpp"
#include <iostream> // For constructor/destructor messages
#include "InputManager.hpp" // Include InputManager header
#include <stdexcept> // For std::runtime_error
#define GLM_ENABLE_EXPERIMENTAL // Enable experimental GLM features
#include "world/Chunk.hpp" // Include Chunk for dimension constants
#include <glm/gtx/compatibility.hpp> // For glm::lerp

// Define the static constants
const float Camera::BASE_MOVE_SPEED = 5.0f; // Units per second
const float Camera::SPRINT_MULTIPLIER = 2.5f; // Multiplier for sprint speed

Camera::Camera(std::shared_ptr<InputManager> inputManager)
    : m_inputManager(inputManager),
      m_absoluteChunkPos(0, 0, 0), // Start at chunk (0,0,0)
      m_localPositionInChunk(0.0f, 0.0f, 3.0f), // Start at local (0,0,3) within chunk (0,0,0)
      m_worldUp(0.0f, 1.0f, 0.0f), // Y is up
      m_yaw(-90.0f), // Pointing down negative Z-axis
      m_pitch(0.0f),
      m_fov(45.0f),
      m_mouseSensitivity(0.05f), // Correctly initialize the member variable m_mouseSensitivity
      m_horizontalVelocity(0.0f), // Initialize horizontal velocity
      m_verticalVelocity(0.0f) {   // Initialize vertical velocity
    if (!m_inputManager) {
        throw std::runtime_error("Camera: InputManager shared_ptr cannot be null!");
    }
    updateCameraVectors();
    std::cout << "Camera created." << std::endl;
}

Camera::~Camera() {
    std::cout << "Camera destroyed." << std::endl;
}

void Camera::updateCameraVectors() {
    // Calculate the new Front vector
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    newFront.y = sin(glm::radians(m_pitch));
    newFront.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_front = glm::normalize(newFront);
    // Also re-calculate the Right and Up vector
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up    = glm::normalize(glm::cross(m_right, m_front));
}

void Camera::update(float deltaTime) {
    if (!m_inputManager) return;

    // Determine current move speed based on sprint key
    float currentMoveSpeed = BASE_MOVE_SPEED;
    if (m_inputManager->isKeyDown(KeyCode::LeftControl)) {
        currentMoveSpeed *= SPRINT_MULTIPLIER;
    }

    // --- Horizontal keyboard movement (WASD) ---
    glm::vec3 targetHorizontalVelocity(0.0f);

    // Define the forward direction in the horizontal plane based purely on yaw
    // This ensures W/S movement is always horizontal, regardless of pitch.
    glm::vec3 horizontalForward(cos(glm::radians(m_yaw)), 0.0f, sin(glm::radians(m_yaw)));
    // The 'right' vector (calculated in updateCameraVectors as cross(front, worldUp))
    // is already constrained to the horizontal plane, effectively:
    // glm::vec3 horizontalRight(-sin(glm::radians(yaw)), 0.0f, cos(glm::radians(yaw)));
    // So, 'right' can be used directly for A/D strafing.

    if (m_inputManager->isKeyDown(KeyCode::W)) {
        targetHorizontalVelocity += horizontalForward * currentMoveSpeed; // Use local horizontalForward
    }
    if (m_inputManager->isKeyDown(KeyCode::S)) {
        targetHorizontalVelocity -= horizontalForward * currentMoveSpeed; // Use local horizontalForward
    }
    if (m_inputManager->isKeyDown(KeyCode::A)) {
        targetHorizontalVelocity -= m_right * currentMoveSpeed;
    }
    if (m_inputManager->isKeyDown(KeyCode::D)) {
        targetHorizontalVelocity += m_right * currentMoveSpeed;
    }

    // --- Vertical keyboard movement (Space/Shift) ---
    float targetVerticalVelocity = 0.0f;
    if (m_inputManager->isKeyDown(KeyCode::Space)) {
        targetVerticalVelocity += BASE_MOVE_SPEED; // Vertical movement uses base speed
    }
    if (m_inputManager->isKeyDown(KeyCode::LeftShift)) {
        targetVerticalVelocity -= BASE_MOVE_SPEED; // Vertical movement uses base speed
    }

    // --- Apply Momentum ---
    // Smoothly interpolate current horizontal velocity towards target
    float horizontalSmoothingFactor = 5.0f; // Current smoothing for W,A,S,D
    m_horizontalVelocity = glm::lerp(m_horizontalVelocity, targetHorizontalVelocity, horizontalSmoothingFactor * deltaTime);

    // Smoothly interpolate current vertical velocity towards target (snappier)
    float verticalSmoothingFactor = 15.0f; // Higher value for snappier vertical movement
    m_verticalVelocity = glm::lerp(m_verticalVelocity, targetVerticalVelocity, verticalSmoothingFactor * deltaTime);

    // --- Update Position ---
    glm::vec3 deltaPosition = m_horizontalVelocity * deltaTime + m_worldUp * m_verticalVelocity * deltaTime;
    m_localPositionInChunk += deltaPosition;

    // --- Handle crossing chunk boundaries ---
    // Check each dimension and update absolute chunk position and local position

    if (m_localPositionInChunk.x < 0.0f) {
        m_absoluteChunkPos.x--;
        m_localPositionInChunk.x += CHUNK_WIDTH;
    } else if (m_localPositionInChunk.x >= CHUNK_WIDTH) {
        m_absoluteChunkPos.x++;
        m_localPositionInChunk.x -= CHUNK_WIDTH;
    }

    if (m_localPositionInChunk.y < 0.0f) {
        m_absoluteChunkPos.y--;
        m_localPositionInChunk.y += CHUNK_HEIGHT;
    } else if (m_localPositionInChunk.y >= CHUNK_HEIGHT) {
        m_absoluteChunkPos.y++;
        m_localPositionInChunk.y -= CHUNK_HEIGHT;
    }

    if (m_localPositionInChunk.z < 0.0f) {
        m_absoluteChunkPos.z--;
        m_localPositionInChunk.z += CHUNK_DEPTH;
    } else if (m_localPositionInChunk.z >= CHUNK_DEPTH) {
        m_absoluteChunkPos.z++;
        m_localPositionInChunk.z -= CHUNK_DEPTH;
    }

    // --- Apply Drag ---
    // Optional: Apply drag to slow down when no keys are pressed
    float dragFactor = 2.0f; // Adjust drag strength as needed
    float drag = 1.0f - glm::clamp(dragFactor * deltaTime, 0.0f, 1.0f);
    m_horizontalVelocity *= drag;
    m_verticalVelocity *= drag;

    // --- Mouse look (direct, no momentum) ---
    double deltaX = m_inputManager->getMouseDeltaX();
    double deltaY = m_inputManager->getMouseDeltaY();

    // Apply sensitivity
    // Note: GLFW's Y delta might be inverted depending on how you want to interpret it.
    // Typically, a positive deltaY from GLFW means the mouse moved down.
    // If you want moving the mouse up to make the camera pitch up, you might need to negate deltaY.
    // Let's assume standard: mouse up -> pitch up, mouse right -> yaw right (increases yaw)
    m_yaw   += static_cast<float>(deltaX) * m_mouseSensitivity;
    m_pitch -= static_cast<float>(deltaY) * m_mouseSensitivity; // Subtract because positive Y delta is downwards

    // Constrain pitch to avoid flipping
    if (m_pitch > 89.0f)
        m_pitch = 89.0f;
    if (m_pitch < -89.0f)
        m_pitch = -89.0f;

    // Always update camera vectors if position or orientation changed
    updateCameraVectors();
}

// Get the camera's position relative to the provided rebase origin for rendering
glm::vec3 Camera::getPositionRelativeTo(glm::ivec3 rebaseOriginChunkCoord) const {
    // Calculate the relative chunk coordinate
    glm::ivec3 relativeChunkPos = m_absoluteChunkPos - rebaseOriginChunkCoord;
    // Calculate the world position of the relative chunk origin
    glm::vec3 relativeChunkWorldPos = glm::vec3(relativeChunkPos.x * CHUNK_WIDTH,
                                               relativeChunkPos.y * CHUNK_HEIGHT,
                                               relativeChunkPos.z * CHUNK_DEPTH);
    // Add the local position within the chunk
    return relativeChunkWorldPos + m_localPositionInChunk;
}

glm::mat4 Camera::getViewMatrix(glm::ivec3 rebaseOriginChunkCoord) const {
    // Pass rebaseOriginChunkCoord to getPositionRelativeTo, which no longer needs chunkDimensions
    glm::vec3 relativePosition = getPositionRelativeTo(rebaseOriginChunkCoord);
    return glm::lookAt(relativePosition, relativePosition + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    float currentFov = glm::clamp(m_fov, 1.0f, 120.0f);
    glm::mat4 proj = glm::perspective(glm::radians(currentFov), aspectRatio, 0.1f, 100.0f); // Near/Far planes
    proj[1][1] *= -1; // GLM is designed for OpenGL; Vulkan's Y-coordinate in clip space is inverted.
    return proj;
}

// --- Getters ---
float Camera::getYaw() const { return m_yaw; }
float Camera::getPitch() const { return m_pitch; }
float Camera::getFov() const { return m_fov; }
glm::vec3 Camera::getFront() const { return m_front; }

// --- Setters ---
void Camera::setPosition(const glm::vec3& position) {
    // This setter is now ambiguous (absolute or relative?). Consider removing or clarifying.
    // For now, let's assume it sets the absolute position.
    m_absoluteChunkPos = glm::ivec3(std::floor(position.x / CHUNK_WIDTH),
                                    std::floor(position.y / CHUNK_HEIGHT),
                                    std::floor(position.z / CHUNK_DEPTH));
    m_localPositionInChunk = position - glm::vec3(m_absoluteChunkPos.x * CHUNK_WIDTH,
                                                 m_absoluteChunkPos.y * CHUNK_HEIGHT,
                                                 m_absoluteChunkPos.z * CHUNK_DEPTH);
}

void Camera::setYaw(float yaw) {
    m_yaw = yaw;
    updateCameraVectors(); // Orientation changed
}

void Camera::setPitch(float pitch) {
    m_pitch = pitch;
    // Constrain pitch
    if (m_pitch > 89.0f) m_pitch = 89.0f;
    if (m_pitch < -89.0f) m_pitch = -89.0f;
    updateCameraVectors(); // Orientation changed
}

void Camera::setFov(float fov) {
    m_fov = fov;
    // No need to call updateCameraVectors() as FOV only affects projection.
    // Projection matrix will use the new FOV when getProjectionMatrix() is called.
}

// --- Origin Rebasing Methods (Removed/Modified) ---

// getAbsolutePosition is no longer needed as camera tracks absolute pos internally
// glm::vec3 Camera::getAbsolutePosition(glm::ivec3 currentRebaseOriginChunkCoord, glm::ivec3 chunkDimensions) const {
//     // Calculate the world position of the origin of the rebase chunk
//     glm::vec3 rebaseOriginWorldPos = glm::vec3(
//         currentRebaseOriginChunkCoord.x * chunkDimensions.x,
//         currentRebaseOriginChunkCoord.y * chunkDimensions.y,
//         currentRebaseOriginChunkCoord.z * chunkDimensions.z
//     );
//     // The camera's m_position is relative to this rebase origin
//     return rebaseOriginWorldPos + m_position;
// }

// rebase is no longer needed as camera always tracks absolute pos
// void Camera::rebase(glm::ivec3 newRebaseOriginChunkCoord, glm::ivec3 oldRebaseOriginChunkCoord, glm::ivec3 chunkDimensions) {
//     // The camera's absolute position (m_absoluteChunkPos + m_localPositionInChunk) remains constant.
//     // The "rebasing" happens when getPositionRelativeTo is called for rendering.
// }