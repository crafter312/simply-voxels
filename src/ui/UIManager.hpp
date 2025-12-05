#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include <glm/glm.hpp> // For glm::vec3, glm::ivec3
#include "../render/VulkanCommon.hpp" // Include for GLFW types
#include <glm/vec3.hpp>
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

    // Draws a non-interactive overlay with XYZ coordinates.
    void drawXYZCoordinateOverlay(const glm::ivec3& absoluteChunkPos, const glm::vec3& localPositionInChunk);

private:
    HelloVulkanApp& m_app; // Reference to the main app to interact with game state
};

#endif // UI_MANAGER_HPP