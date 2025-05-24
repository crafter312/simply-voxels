#include "Camera.hpp"
#include <iostream> // For constructor/destructor messages
#include "InputManager.hpp" // Include InputManager header
#include <stdexcept> // For std::runtime_error
#define GLM_ENABLE_EXPERIMENTAL // Enable experimental GLM features
#include "world/Chunk.hpp" // Include Chunk for dimension constants
#include <glm/gtx/compatibility.hpp> // For glm::lerp


Camera::Camera(std::shared_ptr<InputManager> inputManager)
    : m_inputManager(inputManager),
      m_absoluteChunkPos(0, 0, 0), // Start at chunk (0,0,0)
      m_localPositionInChunk(0.0f, 0.0f, 3.0f), // Start at local (0,0,3) within chunk (0,0,0)
      m_worldUp(0.0f, 1.0f, 0.0f), // Y is up
      m_yaw(-90.0f), // Pointing down negative Z-axis
      m_pitch(0.0f),
      m_fov(45.0f),
      m_mouseSensitivity(0.05f) { // Correctly initialize the member variable m_mouseSensitivity
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

    // --- Mouse look for camera orientation ---
    // Player class now handles position. Camera only handles orientation via mouse.
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

    // Update camera vectors based on new orientation.
    // Position is set by the Player via setPosition().
    // The chunk boundary logic is now handled within setPosition()
    // to ensure m_absoluteChunkPos and m_localPositionInChunk are the canonical representation.
    // The m_position member has been removed, as m_absoluteChunkPos and m_localPositionInChunk
    // are sufficient to define the camera's location.
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
    // Use the constexpr near and far plane distances defined in Camera.hpp
    glm::mat4 proj = glm::perspective(glm::radians(currentFov), 
                                      aspectRatio, 
                                      Camera::NEAR_PLANE_DISTANCE, 
                                      Camera::FAR_PLANE_DISTANCE);
    proj[1][1] *= -1; // GLM is designed for OpenGL; Vulkan's Y-coordinate in clip space is inverted.
    return proj;
}

// --- Getters ---
float Camera::getYaw() const { return m_yaw; }
float Camera::getPitch() const { return m_pitch; }
float Camera::getFov() const { return m_fov; }
glm::vec3 Camera::getFront() const { return m_front; }

// --- Setters ---
void Camera::setPosition(const glm::ivec3& absoluteChunkPos, const glm::vec3& localPositionInChunk) {
    // This method directly sets the camera's chunk and local-in-chunk positions.
    // It's called by the Player class.
    m_absoluteChunkPos = absoluteChunkPos;
    m_localPositionInChunk = localPositionInChunk;
    // No need to call updateCameraVectors() as only position changed, not orientation.
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