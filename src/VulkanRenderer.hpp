#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS // Ensure GLM uses radians
#include <glm/glm.hpp>    // For vec3

#include <vector>
#include <memory>   // For std::unique_ptr
#include <optional>
#include <stdexcept>

// Forward declare HelloVulkanApp types needed here
struct QueueFamilyIndices;

// --- Vertex Data ---

struct Vertex {
    glm::vec3 pos;

    // Describe how to pass this vertex data to the vertex shader
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0; // Index of the binding in the array of bindings
        bindingDescription.stride = sizeof(Vertex); // Distance in bytes between consecutive elements
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to the next data entry after each vertex

        return bindingDescription;
    }

    // Describe how to handle vertex input attribute (position)
    static VkVertexInputAttributeDescription getAttributeDescription() {
        VkVertexInputAttributeDescription attributeDescription{};
        attributeDescription.binding = 0; // Which binding the per-vertex data comes from
        attributeDescription.location = 0; // Corresponds to 'location = 0' in the vertex shader
        attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT; // Format of the data (vec3)
        attributeDescription.offset = offsetof(Vertex, pos); // Byte offset of this attribute within the struct

        return attributeDescription;
    }
};

// Define the vertices and indices for a cube
// We'll define these as constants directly in the header for simplicity,
// though for larger models, you'd load them from a file.
const std::vector<Vertex> cubeVertices = {
    // Front face (Z+) - Red
    {{-0.5f, -0.5f,  0.5f}}, // 0 Bottom Left
    {{ 0.5f, -0.5f,  0.5f}}, // 1 Bottom Right
    {{ 0.5f,  0.5f,  0.5f}}, // 2 Top Right
    {{-0.5f,  0.5f,  0.5f}}, // 3 Top Left
    // Back face (Z-) - Cyan
    {{-0.5f, -0.5f, -0.5f}}, // 4 Bottom Left
    {{ 0.5f, -0.5f, -0.5f}}, // 5 Bottom Right
    {{ 0.5f,  0.5f, -0.5f}}, // 6 Top Right
    {{-0.5f,  0.5f, -0.5f}}  // 7 Top Left
};

// Indices define the triangles that make up the cube faces
// Using uint16_t is fine for simple models like a cube (< 65536 vertices)
const std::vector<uint16_t> cubeIndices = {
    // Front face
    0, 1, 2, 2, 3, 0,
    // Back face
    4, 5, 6, 6, 7, 4,
    // Left face
    4, 7, 3, 3, 0, 4,
    // Right face
    1, 5, 6, 6, 2, 1,
    // Top face
    3, 2, 6, 6, 7, 3,
    // Bottom face
    4, 0, 1, 1, 5, 4
};

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