#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <map>
// Forward declare GLFWwindow to avoid including glfw3.h in the header,
// which is good practice to reduce compile times and dependencies.
struct GLFWwindow;

// Placeholder for your actual KeyCode enum/type.
// You'll need to define this properly based on your framework/needs.
enum class KeyCode {
    Unknown, // Default
    W, A, S, D, Space, LeftArrow, RightArrow, UpArrow, DownArrow
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

    // You might also add methods for mouse position, mouse buttons, etc.

private:
    GLFWwindow* m_window; // Store the window pointer
    std::map<KeyCode, bool> currentKeyStates;
    std::map<KeyCode, bool> previousKeyStates;

    // Helper to map our KeyCode enum to GLFW's key codes
    int getGlfwKeyCode(KeyCode key) const;
};

#endif // INPUT_MANAGER_HPP