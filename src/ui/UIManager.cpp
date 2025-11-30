#include "UIManager.hpp"
#include "../HelloVulkanApp.hpp" // Include the full definition for implementation

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
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);

    if (ImGui::Button("Resume", ImVec2(120, 0))) {
        m_app.resumeGame();
    }
    if (ImGui::Button("Quit to Main Menu", ImVec2(120, 0))) {
        m_app.quitToMenu();
    }

    ImGui::End();
}