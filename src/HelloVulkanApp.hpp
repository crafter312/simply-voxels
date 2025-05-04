#ifndef HELLO_VULKAN_APP_HPP
#define HELLO_VULKAN_APP_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <optional> // For queue family indices

// Structure to hold queue family indices
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// Structure to hold swap chain support details
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class HelloVulkanApp {
public:
    void run();

    // Public member to allow callback access
    bool framebufferResized = false;

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
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    // --- Swap Chain ---
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers; // Needed even for clear

    // --- Rendering ---
    VkRenderPass renderPass = VK_NULL_HANDLE; // Needed for framebuffers
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // --- Synchronization ---
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

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
    void createSwapChain();
    void createImageViews();
    void createRenderPass(); // Added
    void createFramebuffers(); // Added
    void createCommandPool();
    void createCommandBuffers(); // Modified for drawing
    void createSyncObjects();

    // --- Drawing ---
    void drawFrame();

    // --- Swap Chain Recreation ---
    void cleanupSwapChain();
    void recreateSwapChain();

    // --- Helpers ---
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    bool isDeviceSuitable(VkPhysicalDevice device);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // Static callback function for GLFW
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

#endif // HELLO_VULKAN_APP_HPP