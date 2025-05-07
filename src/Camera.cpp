#include "Camera.hpp"
#include <iostream> // For constructor/destructor messages
#include "InputManager.hpp" // Include InputManager header
#include <stdexcept> // For std::runtime_error

Camera::Camera(std::shared_ptr<InputManager> inputManager)
    : m_inputManager(inputManager),
      position(0.0f, 0.0f, 3.0f), // Initial position
      worldUp(0.0f, 1.0f, 0.0f), // Y is up
      yaw(-90.0f), // Pointing down negative Z-axis
      pitch(0.0f),
      fov(45.0f),
      mouseSensitivity(0.1f) { // Added mouse sensitivity
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
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);
    // Also re-calculate the Right and Up vector
    right = glm::normalize(glm::cross(front, worldUp));
    up    = glm::normalize(glm::cross(right, front));
}

void Camera::update(float deltaTime) {
    if (!m_inputManager) return;

    // Keyboard movement
    float cameraSpeed = 5.0f * deltaTime; // Adjusted speed

    if (m_inputManager->isKeyDown(KeyCode::W)) {
        position += front * cameraSpeed;
    }
    if (m_inputManager->isKeyDown(KeyCode::S)) {
        position -= front * cameraSpeed;
    }
    if (m_inputManager->isKeyDown(KeyCode::A)) {
        position -= right * cameraSpeed;
    }
    if (m_inputManager->isKeyDown(KeyCode::D)) {
        position += right * cameraSpeed;
    }
    if (m_inputManager->isKeyDown(KeyCode::Space)) {
        position += worldUp * cameraSpeed; // Use worldUp for consistent up/down
    }
    if (m_inputManager->isKeyDown(KeyCode::LeftShift)) {
        position -= worldUp * cameraSpeed; // Use worldUp for consistent up/down
    }

    // Mouse look
    double deltaX = m_inputManager->getMouseDeltaX();
    double deltaY = m_inputManager->getMouseDeltaY();

    // Apply sensitivity
    // Note: GLFW's Y delta might be inverted depending on how you want to interpret it.
    // Typically, a positive deltaY from GLFW means the mouse moved down.
    // If you want moving the mouse up to make the camera pitch up, you might need to negate deltaY.
    // Let's assume standard: mouse up -> pitch up, mouse right -> yaw right (increases yaw)
    yaw   += static_cast<float>(deltaX) * mouseSensitivity;
    pitch -= static_cast<float>(deltaY) * mouseSensitivity; // Subtract because positive Y delta is downwards

    // Constrain pitch to avoid flipping
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    // Always update camera vectors if position or orientation changed
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    // Ensure FOV is not too small or too large
    float currentFov = glm::clamp(fov, 1.0f, 120.0f);
    glm::mat4 proj = glm::perspective(glm::radians(currentFov), aspectRatio, 0.1f, 100.0f); // Near/Far planes
    proj[1][1] *= -1; // GLM is designed for OpenGL; Vulkan's Y-coordinate in clip space is inverted.
    return proj;
}