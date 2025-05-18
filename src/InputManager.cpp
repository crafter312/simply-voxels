#include "InputManager.hpp"
#include <GLFW/glfw3.h> // Include the GLFW header for glfwGetKey etc.
#include <vector>       // For storing the list of keys to monitor
#include <stdexcept>    // For throwing exceptions on errors

// Define the list of keyboard keys we want our InputManager to track.
static const std::vector<KeyCode> monitoredKeyboardKeys = {
    KeyCode::W, KeyCode::A, KeyCode::S, KeyCode::D, KeyCode::Space, KeyCode::LeftShift, KeyCode::LeftControl,
    KeyCode::LeftArrow, KeyCode::RightArrow, KeyCode::UpArrow, KeyCode::DownArrow
    // Add any other KeyCode enum values you want to track here
};

// Define the list of mouse buttons we want our InputManager to track.
static const std::vector<KeyCode> monitoredMouseButtons = {
    KeyCode::MOUSE_BUTTON_LEFT, KeyCode::MOUSE_BUTTON_RIGHT
    // Add any other mouse button KeyCode enum values you want to track here
};

InputManager::InputManager(GLFWwindow* window) : m_window(window) {
    if (!m_window) {
        throw std::runtime_error("InputManager: GLFWwindow pointer cannot be null!");
    }

    // Initialize all monitored keyboard keys to a 'not pressed' state.
    for (KeyCode key : monitoredKeyboardKeys) {
        currentKeyStates[key] = false;
        previousKeyStates[key] = false;
    }

    // Initialize mouse state
    // m_firstMouse is already true by default from the header
    // Initialize positions to 0, they will be updated on the first call to update()
    // or we can get the initial position here. Let's get it to be safe.
    glfwGetCursorPos(m_window, &m_mouseX, &m_mouseY);
    m_lastMouseX = m_mouseX; // Set last to current initially to avoid a jump
    m_lastMouseY = m_mouseY; // Set last to current initially to avoid a jump

    // Initialize all monitored mouse buttons to a 'not pressed' state.
    for (KeyCode button : monitoredMouseButtons) {
        currentMouseButtonStates[button] = false;
        previousMouseButtonStates[button] = false;
    }
}

int InputManager::getGlfwKeyboardKeyCode(KeyCode key) const {
    switch (key) {
        case KeyCode::W:         return GLFW_KEY_W;
        case KeyCode::A:         return GLFW_KEY_A;
        case KeyCode::S:         return GLFW_KEY_S;
        case KeyCode::D:         return GLFW_KEY_D;
        case KeyCode::Space:     return GLFW_KEY_SPACE;
        case KeyCode::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::LeftArrow: return GLFW_KEY_LEFT;
        case KeyCode::RightArrow:return GLFW_KEY_RIGHT;
        case KeyCode::UpArrow:   return GLFW_KEY_UP;
        case KeyCode::DownArrow: return GLFW_KEY_DOWN;
        // Mouse buttons are not keyboard keys
        case KeyCode::MOUSE_BUTTON_LEFT: // Fall through
        case KeyCode::MOUSE_BUTTON_RIGHT: // Fall through
        case KeyCode::Unknown:    // Fall through
        default:                 return GLFW_KEY_UNKNOWN; // GLFW's code for an unknown key
    }
}

int InputManager::getGlfwMouseButtonCode(KeyCode button) const {
    switch (button) {
        case KeyCode::MOUSE_BUTTON_LEFT:  return GLFW_MOUSE_BUTTON_LEFT;
        case KeyCode::MOUSE_BUTTON_RIGHT: return GLFW_MOUSE_BUTTON_RIGHT;
        // Keyboard keys are not mouse buttons
        default:                          return -1; // Indicate not a valid/mapped mouse button
    }
}

void InputManager::update() {
    if (!m_window) return; // Should not happen if constructor throws, but good practice.

    // Update keyboard states
    // First, copy the current keyboard states to the previous keyboard states.
    previousKeyStates = currentKeyStates;

    // Now, poll GLFW for the current state of all monitored keyboard keys.
    for (KeyCode appKey : monitoredKeyboardKeys) {
        int glfwKey = getGlfwKeyboardKeyCode(appKey);
        if (glfwKey != GLFW_KEY_UNKNOWN) {
            currentKeyStates[appKey] = (glfwGetKey(m_window, glfwKey) == GLFW_PRESS);
        } else {
            // If a KeyCode in monitoredKeyboardKeys doesn't map to a GLFW key,
            // ensure its state is false.
            currentKeyStates[appKey] = false;
        }
    }

    // Update mouse button states
    previousMouseButtonStates = currentMouseButtonStates;
    for (KeyCode mouseButton : monitoredMouseButtons) {
        int glfwButton = getGlfwMouseButtonCode(mouseButton);
        currentMouseButtonStates[mouseButton] = (glfwButton != -1 && glfwGetMouseButton(m_window, glfwButton) == GLFW_PRESS);
    }

    // Update mouse position and calculate delta
    double newMouseX, newMouseY;
    glfwGetCursorPos(m_window, &newMouseX, &newMouseY);

    if (m_firstMouse) {
        // On the very first update, set current and last positions to the new position
        // to ensure the delta is zero for this first frame.
        m_mouseX = newMouseX;
        m_mouseY = newMouseY;
        m_lastMouseX = newMouseX;
        m_lastMouseY = newMouseY;
        m_firstMouse = false;
    } else {
        m_lastMouseX = m_mouseX; // The previous frame's current X is now the last X
        m_lastMouseY = m_mouseY; // The previous frame's current Y is now the last Y
        m_mouseX = newMouseX;    // Update to the new current X
        m_mouseY = newMouseY;    // Update to the new current Y
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

bool InputManager::isMouseButtonDown(KeyCode button) const {
    // Ensure the KeyCode is actually a mouse button, though map lookup will handle non-existence
    if (getGlfwMouseButtonCode(button) == -1) return false;

    auto it = currentMouseButtonStates.find(button);
    if (it != currentMouseButtonStates.end()) {
        return it->second;
    }
    return false; // Button not in our map (shouldn't happen if initialized correctly for monitoredMouseButtons)
}

bool InputManager::isMouseButtonPressed(KeyCode button) const {
    if (getGlfwMouseButtonCode(button) == -1) return false;

    bool currentPressed = isMouseButtonDown(button);
    bool previousPressed = false;
    auto it = previousMouseButtonStates.find(button);
    if (it != previousMouseButtonStates.end()) {
        previousPressed = it->second;
    }
    return currentPressed && !previousPressed;
}

bool InputManager::isMouseButtonReleased(KeyCode button) const {
    if (getGlfwMouseButtonCode(button) == -1) return false;
    bool currentPressed = isMouseButtonDown(button);
    bool previousPressed = false;
    auto it = previousMouseButtonStates.find(button);
    if (it != previousMouseButtonStates.end()) {
        previousPressed = it->second;
    }
    return !currentPressed && previousPressed;
}

double InputManager::getMouseX() const {
    return m_mouseX;
}

double InputManager::getMouseY() const {
    return m_mouseY;
}

double InputManager::getMouseDeltaX() const {
    return m_mouseX - m_lastMouseX;
}

double InputManager::getMouseDeltaY() const {
    return m_mouseY - m_lastMouseY; // Y typically increases downwards in window coordinates
}