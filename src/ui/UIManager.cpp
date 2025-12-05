#include "UIManager.hpp"
#include "../HelloVulkanApp.hpp" // Include the full definition for implementation
#include "../world/Chunk.hpp" // For CHUNK_SIDE_LENGTH
#include <cmath> // For std::floor
#include <cstdio> // For snprintf
#include <iomanip> // For std::fixed and std::setprecision

// --- UI Configuration Constants ---
// Defines how many decimal places to show for the player's world position.
constexpr unsigned int XYZ_DISPLAY_FRACTIONAL_DIGITS = 4;

UIManager::UIManager(HelloVulkanApp& app) : m_app(app) {}

bool UIManager::drawMainMenu() {
    bool startGame = false;

    // Use ImGui to create a simple main menu window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.4f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 150) * 0.5f);
    if (ImGui::Button("Start Game", ImVec2(150, 50))) {
        startGame = true;
    }

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 150) * 0.5f);
    if (ImGui::Button("Quit", ImVec2(150, 50))) {
        // You can call a public method on HelloVulkanApp to quit
        glfwSetWindowShouldClose(m_app.getWindow(), GLFW_TRUE);
    }

    ImGui::End();

    return startGame;
}

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