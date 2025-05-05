#ifndef HELLO_VULKAN_APP_HPP
#define HELLO_VULKAN_APP_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <optional> // For queue family indices
#include <memory>   // For std::unique_ptr

// Structure to hold queue family indices
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// Forward declare SwapChainSupportDetails (defined in VulkanRenderer.hpp)
struct SwapChainSupportDetails;

// Forward declare VulkanRenderer
class VulkanRenderer;

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
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE; // Window surface for Vulkan
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; // Physical device (GPU)
    VkDevice device = VK_NULL_HANDLE; // Logical device

    // --- Queues (Needed by Renderer) ---
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    // --- Swap Chain Info ---
    std::unique_ptr<SwapChainSupportDetails> swapChainSupportDetails; // Store details queried during device picking

    // --- Renderer ---
    std::unique_ptr<VulkanRenderer> renderer;

    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();

    // --- Vulkan Setup Steps ---
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();

    // --- Helpers ---
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);

    // Static callback function for GLFW
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

#endif // HELLO_VULKAN_APP_HPP