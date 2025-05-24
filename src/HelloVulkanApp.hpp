#ifndef HELLO_VULKAN_APP_HPP
#define HELLO_VULKAN_APP_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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
class World;

// Forward declare Player
class Player;

// Forward declare VulkanDebug
class VulkanDebug;

class HelloVulkanApp {
public:
    HelloVulkanApp();
    ~HelloVulkanApp();
    
    // Main function to run the application
    void run();

private:
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

    // --- World Data ---
    std::unique_ptr<World> world;

    // --- Player ---
    std::unique_ptr<Player> player;

    // --- Renderer ---
    std::unique_ptr<VulkanRenderer> renderer;

    // --- Timing ---
    float lastFrameTime = 0.0f;

    // --- Game State ---
    bool m_isPaused = false; // Tracks if the game is paused


    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();

    // --- Vulkan Setup Steps ---
    void createInstance();
    void createSurface();
    
    // Static callback function for GLFW
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

#endif // HELLO_VULKAN_APP_HPP