#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <memory>   // For std::unique_ptr
#include <optional>
#include <stdexcept>

// Forward declare HelloVulkanApp types needed here
struct QueueFamilyIndices;

// Structure to hold swap chain support details (Moved from HelloVulkanApp.hpp)
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanRenderer {
public:
    // Constructor takes necessary handles and pre-queried support details
    VulkanRenderer(GLFWwindow* glfwWindow, VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice logicalDevice, QueueFamilyIndices queueIndices, VkQueue graphicsQueueHandle, VkQueue presentQueueHandle, const std::unique_ptr<SwapChainSupportDetails> swapChainSupport);
    ~VulkanRenderer(); // Use destructor for cleanup

    // Call this after constructor to create pipeline resources
    void init();

    // Main drawing function
    void drawFrame();

    // Public flag to signal resize from callback
    bool framebufferResized = false;

    // Made static, takes surface as argument
    static std::unique_ptr<SwapChainSupportDetails> querySwapChainSupport(VkPhysicalDevice targetDevice, VkSurfaceKHR surface);

private:
    // --- References to external objects ---
    GLFWwindow* window;
    VkInstance instanceRef; // Keep refs to objects managed by HelloVulkanApp
    VkSurfaceKHR surfaceRef;
    VkPhysicalDevice physicalDeviceRef;
    VkDevice deviceRef;
    std::unique_ptr<QueueFamilyIndices> queueIndicesRef;
    VkQueue graphicsQueueRef;
    std::unique_ptr<SwapChainSupportDetails> swapChainSupportRef; // Store pre-queried details
    VkQueue presentQueueRef;

    // --- Constants ---
    const int MAX_FRAMES_IN_FLIGHT = 2;

    // --- Swap Chain ---
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // --- Rendering ---
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // --- Synchronization ---
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    // --- Setup Steps (Internal) ---
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void cleanupSwapChain();
    void recreateSwapChain();

    // --- Helpers (Internal) ---
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};

#endif // VULKAN_RENDERER_HPP