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

    // Public members for easier manipulation in this example
    // In a more robust system, you'd use methods to modify these
    glm::vec3 position;
    float yaw;   // In degrees
    float pitch; // In degrees
    float fov;   // In degrees

private:
    std::shared_ptr<InputManager> m_inputManager; // Store the InputManager
    float mouseSensitivity;

    void updateCameraVectors();

    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
};

#endif // CAMERA_HPP