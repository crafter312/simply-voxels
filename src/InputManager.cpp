#include "InputManager.hpp"
#include <GLFW/glfw3.h> // Include the GLFW header for glfwGetKey etc.
#include <vector>       // For storing the list of keys to monitor
#include <stdexcept>    // For throwing exceptions on errors

// Define the list of keys we want our InputManager to track.
// This makes it easy to add or remove keys without changing loop logic much.
static const std::vector<KeyCode> monitoredKeys = {
    KeyCode::W, KeyCode::A, KeyCode::S, KeyCode::D, KeyCode::Space,
    KeyCode::LeftArrow, KeyCode::RightArrow, KeyCode::UpArrow, KeyCode::DownArrow
    // Add any other KeyCode enum values you want to track here
};

InputManager::InputManager(GLFWwindow* window) : m_window(window) {
    if (!m_window) {
        throw std::runtime_error("InputManager: GLFWwindow pointer cannot be null!");
    }

    // Initialize all monitored keys to a 'not pressed' state.
    for (KeyCode key : monitoredKeys) {
        currentKeyStates[key] = false;
        previousKeyStates[key] = false;
    }
}

int InputManager::getGlfwKeyCode(KeyCode key) const {
    switch (key) {
        case KeyCode::W:         return GLFW_KEY_W;
        case KeyCode::A:         return GLFW_KEY_A;
        case KeyCode::S:         return GLFW_KEY_S;
        case KeyCode::D:         return GLFW_KEY_D;
        case KeyCode::Space:     return GLFW_KEY_SPACE;
        case KeyCode::LeftArrow: return GLFW_KEY_LEFT;
        case KeyCode::RightArrow:return GLFW_KEY_RIGHT;
        case KeyCode::UpArrow:   return GLFW_KEY_UP;
        case KeyCode::DownArrow: return GLFW_KEY_DOWN;
        case KeyCode::Unknown:   // Fall through
        default:                 return GLFW_KEY_UNKNOWN; // GLFW's code for an unknown key
    }
}

void InputManager::update() {
    if (!m_window) return; // Should not happen if constructor throws, but good practice.

    // First, copy the current states to the previous states.
    previousKeyStates = currentKeyStates;

    // Now, poll GLFW for the current state of all monitored keys.
    for (KeyCode appKey : monitoredKeys) {
        int glfwKey = getGlfwKeyCode(appKey);
        if (glfwKey != GLFW_KEY_UNKNOWN) {
            currentKeyStates[appKey] = (glfwGetKey(m_window, glfwKey) == GLFW_PRESS);
        } else {
            // If a KeyCode in monitoredKeys doesn't map to a GLFW key,
            // ensure its state is false.
            currentKeyStates[appKey] = false;
        }
    }
}

bool InputManager::isKeyDown(KeyCode key) const {
    auto it = currentKeyStates.find(key);
    if (it != currentKeyStates.end()) {
        return it->second; // Return true if pressed, false if not or not tracked.
    }
    return false; // Key not in our map (shouldn't happen if initialized correctly for monitoredKeys)
}

bool InputManager::isKeyPressed(KeyCode key) const {
    bool currentPressed = isKeyDown(key);
    bool previousPressed = false;
    auto it = previousKeyStates.find(key);
    if (it != previousKeyStates.end()) {
        previousPressed = it->second;
    }
    return currentPressed && !previousPressed; // True if currently down AND previously up.
}

bool InputManager::isKeyReleased(KeyCode key) const {
    bool currentPressed = isKeyDown(key);
    bool previousPressed = false;
    auto it = previousKeyStates.find(key);
    if (it != previousKeyStates.end()) {
        previousPressed = it->second;
    }
    return !currentPressed && previousPressed; // True if currently up AND previously down.
}