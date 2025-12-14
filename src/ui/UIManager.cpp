#include "UIManager.hpp"
#include "../HelloVulkanApp.hpp" // Include the full definition for implementation
#include "../world/Chunk.hpp" // For CHUNK_SIDE_LENGTH
#include "../world/SaveGameManager.hpp" // For to render available worlds in list and create new worlds
#include <cmath> // For std::floor
#include <cstdio> // For snprintf
#include <chrono>
#include <ctime>
#include <iomanip> // For std::fixed and std::setprecision

// --- UI Configuration Constants ---
// Defines how many decimal places to show for the player's world position.
constexpr unsigned int XYZ_DISPLAY_FRACTIONAL_DIGITS = 4;

UIManager::UIManager(HelloVulkanApp& app, SaveGameManager& saveGameManager)
    : m_app(app), m_saveGameManager(saveGameManager) {}

/******** MAIN MENU FUNCTIONS ********/

std::optional<WorldMetadata> UIManager::drawMainMenu() {
    std::optional<WorldMetadata> worldToLoad = std::nullopt;

    switch (m_currentMenuScreen) {
        case MenuScreenState::ROOT:
            drawMainMenuRoot();
            break;
        case MenuScreenState::WORLD_SELECT:
            worldToLoad = drawMainMenuWorldSelect();
            break;
        case MenuScreenState::CREATE_WORLD:
            worldToLoad = drawMainMenuCreateWorld();
            break;
    }

    return worldToLoad;
}

void UIManager::drawMainMenuRoot() {
    // Use ImGui to create a simple main menu window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main Menu Root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.4f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 150) * 0.5f);
    if (ImGui::Button("Select World", ImVec2(150, 50))) {
        m_currentMenuScreen = MenuScreenState::WORLD_SELECT;
    }

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 150) * 0.5f);
    if (ImGui::Button("Quit", ImVec2(150, 50))) {
        // You can call a public method on HelloVulkanApp to quit
        glfwSetWindowShouldClose(m_app.getWindow(), GLFW_TRUE);
    }

    ImGui::End();
}

std::optional<WorldMetadata> UIManager::drawMainMenuWorldSelect() {
    std::optional<WorldMetadata> worldToLoad = std::nullopt;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("World Select", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 200) * 0.5f);
    if (ImGui::Button("Create New World", ImVec2(200, 40))) {
        m_currentMenuScreen = MenuScreenState::CREATE_WORLD;
    }

    ImGui::Separator();

    // Scrollable list of worlds
    ImGui::BeginChild("WorldList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), true);

    const auto& worlds = m_saveGameManager.getAvailableWorlds();
    for (const auto& world : worlds) {
        if (ImGui::Button(world.worldName.c_str(), ImVec2(-1, 60))) {
            worldToLoad = world;
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Seed: %lld", world.seed);
        ImGui::Text("Last Played: %s", formatTimestamp(world.lastPlayedTimestamp).c_str());
        ImGui::Text("Created: %s", formatTimestamp(world.creationTimestamp).c_str());
        ImGui::EndGroup();
        ImGui::Separator();
    }

    ImGui::EndChild();

    // Back button at the bottom
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 150) * 0.5f);
    if (ImGui::Button("Back", ImVec2(150, 50))) {
        m_currentMenuScreen = MenuScreenState::ROOT;
    }

    ImGui::End();

    return worldToLoad;
}

std::optional<WorldMetadata> UIManager::drawMainMenuCreateWorld() {
    std::optional<WorldMetadata> worldToLoad = std::nullopt;

    static char worldNameBuffer[128] = "New World";
    static char seedBuffer[64] = "";
    static bool useRandomSeed = true;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Create World", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.3f);

    // Center the content
    float contentWidth = 300.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - contentWidth) * 0.5f);
    ImGui::BeginChild("CreateWorldContent", ImVec2(contentWidth, 200), false);

    ImGui::Text("World Name:");
    ImGui::InputText("##WorldName", worldNameBuffer, sizeof(worldNameBuffer));

    ImGui::Checkbox("Random Seed", &useRandomSeed);
    if (!useRandomSeed) {
        ImGui::Text("Seed:");
        ImGui::InputText("##Seed", seedBuffer, sizeof(seedBuffer), ImGuiInputTextFlags_CharsDecimal);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Create", ImVec2(contentWidth, 40))) {
        std::optional<int64_t> seed = std::nullopt;
        if (!useRandomSeed && strlen(seedBuffer) > 0) {
            try {
                seed = std::stoll(seedBuffer);
            } catch (const std::exception& e) {
                // Handle invalid seed input, maybe show an error message
                std::cerr << "Invalid seed format: " << e.what() << std::endl;
            }
        }
        // Create the world and return its metadata to start the game
        worldToLoad = m_saveGameManager.createNewWorld(worldNameBuffer, seed);
    }

    if (ImGui::Button("Cancel", ImVec2(contentWidth, 40))) {
        m_currentMenuScreen = MenuScreenState::WORLD_SELECT;
    }

    ImGui::EndChild();

    ImGui::End();

    return worldToLoad;
}

/******** PAUSE MENU FUNCTIONS ********/

void UIManager::drawPauseMenu() {
    // Center the pause menu window
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(250, 150)); // Set a fixed size for the pause menu window

    ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // Define button dimensions
    float buttonWidth = 200.0f;
    float buttonHeight = 50.0f; // Consistent height for buttons

    // Calculate total height of buttons + spacing for vertical centering
    float totalButtonsHeight = (buttonHeight * 2) + ImGui::GetStyle().ItemSpacing.y;
    float startY = (ImGui::GetWindowHeight() - totalButtonsHeight) * 0.5f;

    // Set cursor position for the first button (vertically centered group, horizontally centered button)
    ImGui::SetCursorPosY(startY);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
    if (ImGui::Button("Resume", ImVec2(buttonWidth, buttonHeight))) {
        m_app.resumeGame();
    }
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f); // Center the next button horizontally
    if (ImGui::Button("Quit to Main Menu", ImVec2(buttonWidth, buttonHeight))) {
        m_app.quitToMenu();
    }
    ImGui::End();
}

/******** OTHER GUI FUNCTIONS ********/

void UIManager::drawLoadingScreen() {
    // Create a full-screen, non-interactive window for the loading message.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const char* loadingText = "Loading World...";
    ImVec2 textSize = ImGui::CalcTextSize(loadingText);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - textSize.x) * 0.5f);
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - textSize.y) * 0.5f);
    ImGui::Text("%s", loadingText);

    ImGui::End();
}

std::string UIManager::formatTimestamp(int64_t timestamp) const {
    if (timestamp == 0) {
        return "Never";
    }
    auto tp = std::chrono::system_clock::from_time_t(timestamp);
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    char buffer[26];
    // Use ctime_s on Windows, ctime_r on POSIX
    ctime_s(buffer, sizeof(buffer), &time);
    buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
    return std::string(buffer);
}

void UIManager::drawXYZCoordinateOverlay(const glm::ivec3& absoluteChunkPos, const glm::vec3& localPositionInChunk) {

    // A collection of flags to create a simple, non-interactive overlay window.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(10, 10)); // 10px padding from the top-left corner
    ImGui::Begin("Coordinates", nullptr, flags);

    // Process and display each coordinate axis separately
    const char* axes[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {

        // The display only works correctly if the local position is within [0, CHUNK_SIDE_LENGTH)
        if (localPositionInChunk[i] < 0.f || localPositionInChunk[i] >= static_cast<float>(CHUNK_SIDE_LENGTH)){
            std::cerr << "WARNING: Local position " << axes[i] << " = " << localPositionInChunk[i]
                      << " is out of expected range [0, " << CHUNK_SIDE_LENGTH << "). Coordinate overlay may display incorrectly." << std::endl;
        }
        
        // Calculate the integer (block) part and fractional part separately.
        int64_t blockPart = static_cast<int64_t>(absoluteChunkPos[i]) * CHUNK_SIDE_LENGTH + static_cast<int64_t>(std::trunc(localPositionInChunk[i]));
        float fractionalPart = localPositionInChunk[i] - std::trunc(localPositionInChunk[i]);

        // Adjust for negative coordinates to ensure correct display
        bool isNegative = blockPart < 0;
        blockPart += isNegative ? 1 : 0;
        fractionalPart = isNegative ? 1.f - fractionalPart : fractionalPart;

        // To display as "integer.fractional", extract the digits from the fractional part.
        // We always use the absolute value for the fractional digits, as the sign is handled separately.
        const uint32_t multiplier = static_cast<uint32_t>(std::pow(10, XYZ_DISPLAY_FRACTIONAL_DIGITS));
        const uint32_t fractionalDigits = static_cast<uint32_t>(std::abs(fractionalPart) * multiplier);

        // Manually prepend a negative sign if the coordinate is negative and the integer part is 0.
        // The %lld format specifier will handle the sign for all other negative numbers.
        const char* sign = (isNegative && blockPart == 0) ? "-" : "";

        // Dynamically create the format string to respect the configured number of digits.
        char formatString[32];
        snprintf(formatString, sizeof(formatString), "  %%s: %s%%lld.%%0%uu", sign, XYZ_DISPLAY_FRACTIONAL_DIGITS);
        ImGui::Text(formatString, axes[i], blockPart, fractionalDigits);
    }

    ImGui::End();
}