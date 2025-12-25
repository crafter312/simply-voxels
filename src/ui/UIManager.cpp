#include "UIManager.hpp"
#include "../SimplyVoxelsApp.hpp" // Include the full definition for implementation
#include "../world/Chunk.hpp" // For CHUNK_SIDE_LENGTH
#include "../ui/InputManager.hpp" // Include InputManager definition
#include "../world/terrain/TerrainGeneratorManager.hpp"
#include <cmath> // For std::floor
#include <cstdio> // For snprintf
#include <chrono>
#include <ctime>
#include <iomanip> // For std::fixed and std::setprecision
#include <sstream>
#include "imgui_stdlib.h" // For std::string support in ImGui::InputText

// --- UI Configuration Constants ---
// Defines how many decimal places to show for the player's world position.
constexpr unsigned int XYZ_DISPLAY_FRACTIONAL_DIGITS = 4;

UIManager::UIManager(SimplyVoxelsApp& app, InputManager& inputMgr) : m_app(app), m_inputManager(inputMgr) {
    m_saveGameManager = std::make_unique<SaveGameManager>("../run/saves/");
    if (!m_saveGameManager) throw std::runtime_error("Failed to create SaveGameManager!");
}

/******** MAIN MENU FUNCTIONS ********/

std::optional<WorldMetadata> UIManager::drawMainMenu(const std::vector<TerrainGeneratorEntry>& generators) {
    std::optional<WorldMetadata> worldToLoad = std::nullopt;

    switch (m_currentMenuScreen) {
        case MenuScreenState::ROOT:
            drawMainMenuRoot();
            break;
        case MenuScreenState::WORLD_SELECT:
            worldToLoad = drawMainMenuWorldSelect();
            break;
        case MenuScreenState::CREATE_WORLD:
            worldToLoad = drawMainMenuCreateWorld(generators);
            break;
    }

    if (worldToLoad.has_value()) {
        m_currentMenuScreen = MenuScreenState::ROOT; // reset to root menu after selecting/creating a world
        m_saveGameManager->updateLastPlayed(worldToLoad.value().directoryName); // world is selected to be loaded, update last played timestamp
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
        // You can call a public method on SimplyVoxelsApp to quit
        glfwSetWindowShouldClose(m_app.getWindow(), GLFW_TRUE);
    }

    ImGui::End();
}

std::optional<WorldMetadata> UIManager::drawMainMenuWorldSelect() {
    std::optional<WorldMetadata> worldToLoad = std::nullopt;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("World Select", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    // --- Header Section (Fixed) ---
    {
        ImGui::BeginChild("Header", ImVec2(0, 50), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 200) * 0.5f);
        if (ImGui::Button("Create New World", ImVec2(200, 40))) {
            // Cancel any pending rename if user clicks "Create New World"
            m_worldPendingRename.reset();
            m_renameBuffer.clear();
            m_currentMenuScreen = MenuScreenState::CREATE_WORLD;
        }
        ImGui::Separator();
        ImGui::EndChild();
    }

    // --- Content Section (Scrollable) ---
    // Calculate the height for the scrollable area, leaving space for the footer.
    float footerHeight = 70.0f;
    ImVec2 contentSize = ImVec2(0, -footerHeight);
    {
        ImGui::BeginChild("WorldList", contentSize, true);

        const auto& worlds = m_saveGameManager->getAvailableWorlds();
        for (int i = 0; i < worlds.size(); ++i) {
            const auto& world = worlds[i];

            ImGui::PushID(i); // Use index for a unique ID

            // Create a custom button with more complex content
            ImVec2 buttonSize = ImVec2(-1, 60); // Full width, 60 pixels high
            ImGui::Button("##world_button", buttonSize);

            // Get the bounding box of the invisible button. We'll use this for positioning and hit-testing.
            ImVec2 rectMin = ImGui::GetItemRectMin();
            ImVec2 rectMax = ImGui::GetItemRectMax();

            // Normal left-click action to load the world.
            // This is more robust because it checks for a click within the button's specific rectangle.
            // We also check if the window is hovered, but allow the hover state even if it's blocked
            // by an active item (like a popup or text input). This prevents the click from firing
            // when a modal or context menu is the intended target of the click.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(rectMin, rectMax) &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                // Cancel any pending rename if user clicks to load a world
                m_worldPendingRename.reset();
                m_renameBuffer.clear();
                worldToLoad = world;
            }

            // Define padding and layout within the button
            int topMargin = 4;
            int leftMargin = 10;

            // --- Conditional Rendering: Text or InputText for World Name ---            
            if (m_worldPendingRename.has_value() && (m_worldPendingRename->directoryName == world.directoryName)) {
                ImGui::SetCursorScreenPos(ImVec2(rectMin.x + leftMargin - 4, rectMin.y + topMargin - 3));
                ImGui::PushItemWidth(std::min(rectMax.x - rectMin.x - 20, 300.f)); // Set a reasonable width for the input text

                // Automatically focus the input text field on the first frame it appears
                ImGui::SetKeyboardFocusHere();

                // Use ImGui::InputText with the std::string buffer
                if (ImGui::InputText("##Rename", &m_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    // User pressed Enter: commit the rename
                    m_saveGameManager->renameWorld(world.directoryName, m_renameBuffer);
                    m_worldPendingRename.reset(); // Finish renaming
                    m_renameBuffer.clear();
                } else if ((!ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || m_inputManager.isKeyPressed(KeyCode::Escape)) {
                    m_worldPendingRename.reset();
                    m_renameBuffer.clear();
                }

                ImGui::PopItemWidth();
            } else {
                // Default behavior: just draw the world name using ImGui::Text at a specific position
                ImGui::SetCursorScreenPos(ImVec2(rectMin.x + leftMargin, rectMin.y + topMargin));
                ImGui::TextUnformatted(world.worldName.c_str());
            }

            // World Details (smaller font, right-aligned)
            std::string seed_text = "Seed: " + std::to_string(world.seed);
            std::string last_played_text = "Last Played: " + formatTimestamp(world.lastPlayedTimestamp);
            std::string created_text = "Created: " + formatTimestamp(world.creationTimestamp);

            float text_height = ImGui::GetTextLineHeight();
            ImGui::SetCursorScreenPos(ImVec2(rectMin.x + leftMargin, rectMin.y + topMargin + text_height));
            ImGui::TextUnformatted(seed_text.c_str());
            ImGui::SetCursorScreenPos(ImVec2(rectMin.x + leftMargin, rectMin.y + topMargin + text_height * 2));
            ImGui::TextUnformatted(created_text.c_str());
            ImGui::SetCursorScreenPos(ImVec2(rectMin.x + leftMargin, rectMin.y + topMargin + text_height * 3));
            ImGui::TextUnformatted(last_played_text.c_str());

            // Create a unique ID for the popup associated with this specific world item.
            std::string popup_id = "world_context_menu_" + std::to_string(i);

            // Manually detect a right-click within the button's bounds.
            // This is more robust than IsItemClicked() as it doesn't rely on the "last item" state.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(rectMin, rectMax)) {
                // If a rename was in progress, cancel it before opening the context menu.
                m_worldPendingRename.reset();
                m_renameBuffer.clear();
                ImGui::OpenPopup(popup_id.c_str());
            }

            // Draw the unique popup if it's open.
            if (ImGui::BeginPopup(popup_id.c_str())) {
                // Pass the index so the context menu can set the rename state
                drawWorldContextMenu(world);
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::Separator();
        }

        ImGui::EndChild();
    }

    // --- Footer Section (Fixed) ---
    {
        ImGui::BeginChild("Footer", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar); // Takes remaining space
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 150) * 0.5f);
        if (ImGui::Button("Back", ImVec2(150, 50))) {
            // Cancel any pending rename if user clicks "Back"
            m_worldPendingRename.reset();
            m_renameBuffer.clear();
            m_currentMenuScreen = MenuScreenState::ROOT;
        }
        ImGui::EndChild();
    }

    // --- Delete Confirmation Modal ---
    // This is drawn outside the main world list loop.
    // It will appear centered on the screen when m_worldPendingDelete has a value.
    if (m_worldPendingDelete.has_value()) {
        ImGui::OpenPopup("Delete World?");
    }

    // Always center this modal.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Delete World?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete '%s'?", m_worldPendingDelete->worldName.c_str());
        ImGui::Text("This action cannot be undone!");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            m_saveGameManager->deleteWorld(m_worldPendingDelete->directoryName);
            m_worldPendingDelete.reset(); // Clear the state
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            m_worldPendingDelete.reset(); // Clear the state
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();

    return worldToLoad;
}

void UIManager::drawWorldContextMenu(const WorldMetadata& world) {
    // --- Rename Item ---
    std::string renameLabel = "Rename";
    if (ImGui::MenuItem(renameLabel.c_str())) {
        m_worldPendingRename = world;
        m_renameBuffer = world.worldName;
        // The context menu closes automatically, which is what we want.
    }

    // --- Delete Item ---
    std::string deleteLabel = "Delete '" + world.worldName + "'";
    if (ImGui::MenuItem(deleteLabel.c_str())) {
        m_worldPendingDelete = world; // Set state to open the confirmation modal
        // The context menu also closes automatically here.
    }
}

std::optional<WorldMetadata> UIManager::drawMainMenuCreateWorld(const std::vector<TerrainGeneratorEntry>& generators) {
    std::optional<WorldMetadata> worldToLoad = std::nullopt;

    static char worldNameBuffer[128] = "New World";
    static char seedBuffer[64] = "";
    static bool useRandomSeed = true;
    // Selected generator index persists between frames while in the create-world flow
    static int selectedGeneratorIndex = 0;

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

    // --- Terrain Generator Selection ---
    ImGui::Spacing();
    ImGui::Text("Terrain Generator:");

    // Preview label for the combo
    std::string preview = "Default";
    if (!generators.empty()) {
        if (selectedGeneratorIndex < 0 || selectedGeneratorIndex >= static_cast<int>(generators.size())) selectedGeneratorIndex = 0;
        preview = generators[selectedGeneratorIndex].metadata.name + " (" + generators[selectedGeneratorIndex].metadata.id + ")";

        // Keep the full, un-truncated preview for tooltip display
        std::string fullPreview = preview;

        // If the preview text is wider than the combo width, truncate with an ellipsis.
        const ImGuiStyle& _style_local = ImGui::GetStyle();
        float innerPadX_local = _style_local.FramePadding.x + 6.0f;
        float maxPreviewW = std::max(40.0f, contentWidth - innerPadX_local * 2.0f);
        auto ellipsize = [&](const std::string &s, float maxW) {
            if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
            const std::string ell = "...";
            std::string out = s;
            // Trim until the ellipsized string fits. This is simple and robust.
            while (!out.empty() && ImGui::CalcTextSize((out + ell).c_str()).x > maxW) out.pop_back();
            return out + ell;
        };

        preview = ellipsize(preview, maxPreviewW);
        // store fullPreview in an outer scope variable by moving it out
        // (we'll use it after the combo to show the tooltip)
        // Note: fullPreview already contains the unmodified preview string.
    }

    // Make the combo and its popup match the content width so the right edge lines up
    // with the other buttons in this child. We push an item width that controls the
    // overall combo widget width used by ImGui.
    ImGui::PushItemWidth(contentWidth);
    if (ImGui::BeginCombo("##GeneratorCombo", preview.c_str())) {
        // Maximum description characters to display per option
        const size_t MAX_DESC_CHARS = 200;
        // Wrap width inside each option (leave some padding)
        float maxOptionWrapWidth = std::max(120.0f, contentWidth - 20.0f);

        for (int i = 0; i < static_cast<int>(generators.size()); ++i) {
            const auto& gen = generators[i];

            std::string title = gen.metadata.name;
            std::string authorVer = gen.metadata.author + ", v" + gen.metadata.version;
            std::string desc = gen.metadata.description;
            if (desc.size() > MAX_DESC_CHARS) {
                desc = desc.substr(0, MAX_DESC_CHARS - 3) + "...";
            }

            // Compute available width inside popup and wrap width. Use the
            // `contentWidth` so the popup doesn't become narrower than the
            // surrounding controls and lines up on the right edge.
            float availWidth = contentWidth;
            const ImGuiStyle& style = ImGui::GetStyle();
            const float innerPadX = style.FramePadding.x + 6.0f; // symmetric horizontal padding
            // Calculate wrap width consistently from availWidth and horizontal padding
            float wrapWidth = std::max(120.0f, availWidth - (innerPadX * 2.0f));
            // Small epsilon to avoid a 1-pixel rounding mismatch between CalcTextSize and
            // TextWrapped which can produce an extra empty line when text hits the wrap boundary.
            const float WRAP_EPS = 1.0f;
            float measuredWrap = std::max(8.0f, wrapWidth - WRAP_EPS);

            // Measure text heights. Title/author are single-line (no wrap).
            ImVec2 titleSize = ImGui::CalcTextSize(title.c_str(), nullptr, false, 0.0f);
            ImVec2 authorSize = ImGui::CalcTextSize(authorVer.c_str(), nullptr, false, 0.0f);

            // Deterministic word-wrap for the description so measured and rendered
            // heights always match (avoids TextWrapped/CalcTextSize rounding mismatch).
            auto wrap_words = [&](const std::string &text, float maxWidth) {
                std::vector<std::string> lines;
                std::istringstream iss(text);
                std::string word;
                std::string line;
                while (iss >> word) {
                    std::string test = line.empty() ? word : line + " " + word;
                    ImVec2 sz = ImGui::CalcTextSize(test.c_str());
                    if (sz.x > maxWidth && !line.empty()) {
                        lines.push_back(line);
                        line = word;
                    } else {
                        line = test;
                    }
                }
                if (!line.empty()) lines.push_back(line);
                return lines;
            };

            std::vector<std::string> descLines = wrap_words(desc, measuredWrap);
            ImVec2 descSize = ImVec2(0, 0);
            for (const auto &ln : descLines) {
                ImVec2 s = ImGui::CalcTextSize(ln.c_str(), nullptr, false, 0.0f);
                descSize.y += s.y;
                descSize.x = std::max(descSize.x, s.x);
            }

            // Add style padding and spacing to ensure reserved height matches rendered content
            float itemSpacingY = style.ItemSpacing.y;
            float separatorH = 1.0f;
            // Use symmetric vertical padding (top & bottom). Slightly reduce the top inset
            // so the visual text block appears vertically centered within each option box.
            float padTop = style.FramePadding.y + 2.0f;
            float padBottom = style.FramePadding.y + 2.0f;
            // Reduce vertical spacing a bit because we add small fractional gaps while rendering
            float totalH = titleSize.y + authorSize.y + descSize.y + padTop + padBottom + (itemSpacingY * 0.5f) + separatorH;

            // Reserve layout space and handle clicks explicitly to avoid overlap
            std::string itemId = "gen_item_" + std::to_string(i);
            ImGui::PushID(itemId.c_str());

            // Compute item rect from current cursor and measured total height
            ImVec2 rectMin = ImGui::GetCursorScreenPos();
            ImVec2 rectMax = ImVec2(rectMin.x + availWidth, rectMin.y + totalH);

            // Detect hover/click by checking mouse against the rect directly
            ImVec2 mousePos = ImGui::GetMousePos();
            bool isHoveredRect = (mousePos.x >= rectMin.x && mousePos.x <= rectMax.x && mousePos.y >= rectMin.y && mousePos.y <= rectMax.y) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            bool wasClicked = isHoveredRect && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

            // Draw highlight/background only for the hovered item (avoid constant highlight on selected)
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 bgCol = ImGui::GetColorU32(ImGuiCol_FrameBg);
            if (isHoveredRect) {
                bgCol = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
            }
            drawList->AddRectFilled(rectMin, rectMax, bgCol, style.FrameRounding);

            // Handle click (select and close the combo popup)
            if (wasClicked) {
                selectedGeneratorIndex = i;
                ImGui::CloseCurrentPopup();
            }


            // Render text into the reserved rectangle with precise positioning so all lines align
            float leftX = rectMin.x + innerPadX;
            float wrapPosX = rectMax.x - innerPadX;

            float y = rectMin.y + padTop;

            // Title (single-line)
            ImVec2 titleSz = ImGui::CalcTextSize(title.c_str(), nullptr, false, 0.0f);
            ImGui::SetCursorScreenPos(ImVec2(leftX, y));
            ImGui::TextUnformatted(title.c_str());
            y += titleSz.y;

            // Small vertical gap
            y += style.ItemSpacing.y * 0.25f;

            // Author/version line (aligned with title)
            ImVec2 authorSz = ImGui::CalcTextSize(authorVer.c_str(), nullptr, false, wrapPosX - leftX);
            ImGui::SetCursorScreenPos(ImVec2(leftX, y));
            ImGui::TextUnformatted(authorVer.c_str());
            y += authorSz.y;

            // Small vertical gap before description
            y += style.ItemSpacing.y * 0.25f;

            // Description (may wrap to multiple lines). Render lines produced by the
            // deterministic wrapper so heights match measured values exactly.
            for (const auto &ln : descLines) {
                ImVec2 lnSz = ImGui::CalcTextSize(ln.c_str(), nullptr, false, 0.0f);
                ImGui::SetCursorScreenPos(ImVec2(leftX, y));
                ImGui::TextUnformatted(ln.c_str());
                y += lnSz.y;
            }

            // Advance layout cursor to the end of the reserved rect so next item is placed correctly
            ImGui::SetCursorScreenPos(rectMax);
            ImGui::PopID();
            ImGui::Separator();
        }

        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    // Show full un-truncated preview as a tooltip when hovering the combo preview.
    // Older ImGui versions may not define delay flags; use the basic hovered check here.
    if (!preview.empty() && ImGui::IsItemHovered()) {
        // If we truncated earlier, reconstruct the fullPreview from generators
        std::string fullPreviewTooltip = "";
        if (!generators.empty() && selectedGeneratorIndex >= 0 && selectedGeneratorIndex < static_cast<int>(generators.size())) {
            fullPreviewTooltip = generators[selectedGeneratorIndex].metadata.name + " (" + generators[selectedGeneratorIndex].metadata.id + ")";
        }
        if (!fullPreviewTooltip.empty()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(fullPreviewTooltip.c_str());
            ImGui::EndTooltip();
        }
    }

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
        worldToLoad = m_saveGameManager->createNewWorld(worldNameBuffer, seed);
        if (worldToLoad.has_value() && !generators.empty()) {
            if (selectedGeneratorIndex < 0 || selectedGeneratorIndex >= static_cast<int>(generators.size())) selectedGeneratorIndex = 0;
            worldToLoad->generatorId = generators[selectedGeneratorIndex].metadata.id;
        }
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
    // Use platform-specific thread-safe time-to-string functions
#if defined(_WIN32)
    // Use ctime_s on Windows for thread safety
    ctime_s(buffer, sizeof(buffer), &time);
#else
    // Use ctime_r on POSIX-compliant systems (like Linux) for thread safety
    ctime_r(&time, buffer);
#endif
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