#ifndef HELLO_VULKAN_APP_HPP
#define HELLO_VULKAN_APP_HPP

#include "render/VulkanCommon.hpp" // Centralized header for Volk and GLFW

#include <vector>
#include <optional> // For queue family indices
#include <memory>   // For std::unique_ptr
#include <string>   // For std::string if used (e.g. error messages)

// Forward declare VulkanDevice (now handles physical/logical device and queues)
class VulkanDevice;

// Forward declare SwapChainSupportDetails (defined in VulkanRenderer.hpp)
struct SwapChainSupportDetails;

// Forward declare VulkanRenderer
class VulkanRenderer;

// Forward declare InputManager
class InputManager;

// Forward declare Camera
class Camera;

// Forward declare BlockRegistry
class BlockRegistry;

// Forward declare World
struct WorldMetadata; // Forward declare WorldMetadata
class World;

// Forward declare Player
class Player;

// Forward declare VulkanDebug
class VulkanDebug;

// Forward declare UIManager
class UIManager;

// Forward declare TerrainGeneratorManager
class TerrainGeneratorManager;

class SimplyVoxelsApp {
public:
    SimplyVoxelsApp();
    ~SimplyVoxelsApp();
    
    // Main function to run the application
    void run();

    // --- Public State Changers for UI ---
    void resumeGame();
    void quitToMenu();
    void startGame(const WorldMetadata& worldMeta); // New function to start a game with specific metadata
    GLFWwindow* getWindow() { return window; }

private:
    // Define the possible states of the application
    enum class GameState {
        STARTUP,
        MAIN_MENU,
        LOADING,
        IN_GAME,
        PAUSED
    };

    // --- Constants ---
    const int MAX_FRAMES_IN_FLIGHT = 2; // For frame synchronization

    // --- Core Components ---
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    std::unique_ptr<VulkanDebug> vulkanDebug; // Moved up for clarity with instance
    VkSurfaceKHR surface = VK_NULL_HANDLE; // Window surface for Vulkan

    // --- Vulkan Device Management ---
    std::unique_ptr<VulkanDevice> vulkanDevice;

    // --- Input ---
    std::shared_ptr<InputManager> inputManager;

    // --- Camera ---
    std::shared_ptr<Camera> camera;

    // --- Block Definitions ---
    std::unique_ptr<BlockRegistry> blockRegistry;

    // --- Terrain Generator Manager ---
    std::unique_ptr<TerrainGeneratorManager> m_terrainGeneratorManager;

    // --- World Data ---
    std::unique_ptr<World> world;

    // --- Player ---
    std::unique_ptr<Player> player;

    // --- Renderer ---
    std::unique_ptr<VulkanRenderer> renderer;

    // --- Timing ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    // --- Game State ---
    GameState m_currentState = GameState::STARTUP;

    // --- UI Manager ---
    std::unique_ptr<UIManager> m_uiManager;

    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();

    // --- Vulkan Setup Steps ---
    void createInstance();
    void createSurface();
    
    // --- State-specific update and render functions ---

    // Game startup state
    bool startup();

    void update();
    void render();

    void updateMainMenu();
    void renderMainMenu();

    void updateLoading(float dt);
    void renderLoading();

    void updateInGame(float dt);
    void renderInGame();

    void updatePaused();
    void renderPaused();

    // Static callback function for GLFW
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

#endif // HELLO_VULKAN_APP_HPP