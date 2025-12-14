#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include <optional>
#include <glm/glm.hpp> // For glm::vec3, glm::ivec3
#include "../render/VulkanCommon.hpp" // Include for GLFW types
#include <glm/vec3.hpp>
#include "imgui.h"

#include "../world/WorldMetadata.hpp"

// Forward declaration to avoid circular dependencies
class HelloVulkanApp;
class SaveGameManager;
// Forward declare GLFWwindow to avoid including the full glfw3.h here
struct GLFWwindow;

class UIManager {
public:
    explicit UIManager(HelloVulkanApp& app, SaveGameManager& saveGameManager);

    // Draws the main menu and returns the metadata of a world if one is selected to be loaded.
    std::optional<WorldMetadata> drawMainMenu();

    // Draws the pause menu.
    void drawPauseMenu();

    // Draws the loading screen.
    void drawLoadingScreen();

    // Draws a non-interactive overlay with XYZ coordinates.
    void drawXYZCoordinateOverlay(const glm::ivec3& absoluteChunkPos, const glm::vec3& localPositionInChunk);

private:
    enum class MenuScreenState {
        ROOT,
        WORLD_SELECT,
        CREATE_WORLD
        // Future states like SETTINGS, OPTIONS, CREDITS, etc. can be added here
    };

    HelloVulkanApp& m_app; // Reference to the main app to interact with game state
    SaveGameManager& m_saveGameManager; // Reference to SaveGameManager for world data

    MenuScreenState m_currentMenuScreen = MenuScreenState::ROOT;

    // Private helper functions for drawing different menu screens
    void drawMainMenuRoot();
    std::optional<WorldMetadata> drawMainMenuWorldSelect();
    std::optional<WorldMetadata> drawMainMenuCreateWorld();

    // Helper to format timestamps
    std::string formatTimestamp(int64_t timestamp) const;
};

#endif // UI_MANAGER_HPP