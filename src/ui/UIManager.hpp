#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include <optional>
#include <memory>
#include <string>
#include <glm/glm.hpp> // For glm::vec3, glm::ivec3
#include "../render/VulkanCommon.hpp" // Include for GLFW types
#include <glm/vec3.hpp>
#include "imgui.h"

#include "../world/WorldMetadata.hpp"
#include "../world/SaveGameManager.hpp" // For to render available worlds in list and create new worlds

// Forward declaration to avoid circular dependencies
class SimplyVoxelsApp;
class InputManager;
// Forward declare GLFWwindow to avoid including the full glfw3.h here
struct GLFWwindow;

class UIManager {
public:
    explicit UIManager(SimplyVoxelsApp& app, InputManager& inputMgr);

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

    SimplyVoxelsApp& m_app; // Reference to the main app to interact with game state
    InputManager& m_inputManager; // Reference to input manager for handling UI input

    // --- Save Game Manager ---
    std::unique_ptr<SaveGameManager> m_saveGameManager;

    MenuScreenState m_currentMenuScreen = MenuScreenState::ROOT;

    // --- World Select Context Menu State ---
    std::optional<WorldMetadata> m_worldPendingDelete; // Holds metadata for the world awaiting delete confirmation in a modal.
    std::optional<WorldMetadata> m_worldPendingRename; // Holds metadata for the world being renamed.
    std::string m_renameBuffer;                        // Holds the temporary text for the world being renamed.

    // Private helper functions for drawing different menu screens
    void drawMainMenuRoot();
    std::optional<WorldMetadata> drawMainMenuWorldSelect();
    std::optional<WorldMetadata> drawMainMenuCreateWorld();
    void drawWorldContextMenu(const WorldMetadata& world);

    // Helper to format timestamps
    std::string formatTimestamp(int64_t timestamp) const;
};

#endif // UI_MANAGER_HPP