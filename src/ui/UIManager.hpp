#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include "../render/VulkanCommon.hpp" // Include for GLFW types
#include "imgui.h"

// Forward declaration to avoid circular dependencies
class HelloVulkanApp;
// Forward declare GLFWwindow to avoid including the full glfw3.h here
struct GLFWwindow;

class UIManager {
public:
    explicit UIManager(HelloVulkanApp& app);

    // Draws the main menu and returns true if the "Start Game" button was clicked.
    bool drawMainMenu();

    // Draws the pause menu.
    void drawPauseMenu();

    // Draws the loading screen.
    void drawLoadingScreen();

private:
    HelloVulkanApp& m_app; // Reference to the main app to interact with game state
};

#endif // UI_MANAGER_HPP