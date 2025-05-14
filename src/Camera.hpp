#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory> // Required for std::shared_ptr

// Forward declaration for InputManager
class InputManager;

class Camera {
public:
    // Constructor now accepts a shared_ptr to InputManager
    Camera(std::shared_ptr<InputManager> inputManager);
    ~Camera();

    void update(float deltaTime);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    // Getters
    glm::vec3 getPosition() const;
    float getYaw() const;
    float getPitch() const;
    float getFov() const;
    glm::vec3 getFront() const; // Useful for external logic that might need camera direction
    glm::vec3 getHorizontalVelocity() const;
    float getVerticalVelocity() const;

    // Setters
    void setPosition(const glm::vec3& position);
    void setYaw(float yaw);
    void setPitch(float pitch);
    void setFov(float fov);
    // Note: horizontalVelocity and verticalVelocity are typically managed internally by update()

private:
    std::shared_ptr<InputManager> m_inputManager; // Store the InputManager
    float mouseSensitivity;

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
    float m_fov;   // In degrees
    glm::vec3 m_horizontalVelocity; // For W, A, S, D momentum
    float m_verticalVelocity;     // For Space, Left Shift momentum

    void updateCameraVectors();

public: // Making constants public for potential external reference, or move to private if only internal
    static const float BASE_MOVE_SPEED;
    static const float SPRINT_MULTIPLIER;
};

#endif // CAMERA_HPP