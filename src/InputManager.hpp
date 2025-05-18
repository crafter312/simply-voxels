#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <map>
// Forward declare GLFWwindow to avoid including glfw3.h in the header,
// which is good practice to reduce compile times and dependencies.
struct GLFWwindow;

// KeyCode enum now includes mouse buttons.
enum class KeyCode {
    Unknown, // Default
    W, A, S, D, Space, LeftShift, LeftControl, LeftArrow, RightArrow, UpArrow, DownArrow,
    MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT
    // ... add other keys you need
};

class InputManager {
public:
    // Constructor now takes a GLFWwindow pointer
    InputManager(GLFWwindow* window);

    // Call this once at the beginning of each frame
    void update();

    // Check if a key is currently held down
    bool isKeyDown(KeyCode key) const;

    // Check if a key was pressed down THIS frame
    bool isKeyPressed(KeyCode key) const;

    // Check if a key was released THIS frame
    bool isKeyReleased(KeyCode key) const;

    // Check if a mouse button is currently held down
    bool isMouseButtonDown(KeyCode button) const;

    // Check if a mouse button was pressed down THIS frame
    bool isMouseButtonPressed(KeyCode button) const;

    // Check if a mouse button was released THIS frame
    bool isMouseButtonReleased(KeyCode button) const;

    // Mouse position getters
    double getMouseX() const;
    double getMouseY() const;

    // Mouse delta (change since last frame) getters
    double getMouseDeltaX() const;
    double getMouseDeltaY() const;

private:
    GLFWwindow* m_window; // Store the window pointer
    std::map<KeyCode, bool> currentKeyStates;
    std::map<KeyCode, bool> previousKeyStates;

    std::map<KeyCode, bool> currentMouseButtonStates;
    std::map<KeyCode, bool> previousMouseButtonStates;

    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_firstMouse = true; // To handle initial mouse delta jump

    // Helper to map our KeyCode enum to GLFW's key codes
    int getGlfwKeyboardKeyCode(KeyCode key) const;
    // Helper to map our KeyCode enum (for mouse buttons) to GLFW's mouse button codes
    int getGlfwMouseButtonCode(KeyCode button) const;
};

#endif // INPUT_MANAGER_HPP