#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS // Ensure GLM uses radians
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Ensure GLM uses depth range [0, 1]
#include <glm/glm.hpp>    // For vec3, mat4
#include <glm/gtc/matrix_transform.hpp> // For lookAt, perspective, etc.

#include <vector>
#include <memory>   // For std::unique_ptr
#include <optional>
#include <stdexcept>
#include <string>   // For shader file loading
#include <fstream>  // For shader file loading
#include <array>    // For Vertex attributes
#include <cstdint>  // Required for uint32_t

#include "../resource/ModelLoader.hpp" // Include the ModelLoader which now contains Vertex and ModelData
#include "VulkanDescriptorSetManager.hpp" // Include the new manager


// Forward declare HelloVulkanApp types needed here
class VulkanDevice; // Forward declaration for our wrapper
struct QueueFamilyIndices;
class VulkanBufferManager; // Forward declaration
class VulkanPipelineFactory; // Forward declaration
class Camera; // Forward declaration
class BlockRegistry; // Forward declaration
class ResourceManager; // Forward declaration
class World; // Forward declaration

// --- Uniform Buffer Object ---
// class VulkanTextureLoader; // No longer directly managed here

struct UniformBufferObject {
       // glm::mat4 model; // Model matrix will be handled by push constants
    glm::mat4 view;
    glm::mat4 proj;
};

// Forward declare VulkanSwapChain
class VulkanSwapChain;


class VulkanRenderer {
public:
    // Constructor no longer takes BlockRegistry
    VulkanRenderer(GLFWwindow* glfwWindow, VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice logicalDevice, QueueFamilyIndices queueIndices, VkQueue graphicsQueueHandle, VkQueue presentQueueHandle, std::shared_ptr<Camera> cameraPtr);
    ~VulkanRenderer(); // Use destructor for cleanup

    // Call this after constructor to create pipeline resources, now takes BlockRegistry and World
    void init(const BlockRegistry& blockRegistryRef, const World& worldRef);

    // Main drawing function
    void drawFrame();

    // Public flag to signal resize from callback
    bool framebufferResized = false;

private:
    // --- References to external objects ---
    GLFWwindow* window;
    VkInstance instanceRef; // Keep refs to objects managed by HelloVulkanApp
    VkSurfaceKHR surfaceRef;
    VkPhysicalDevice physicalDeviceRef;
    VkDevice deviceRef;
    // Use shared_ptr as both Renderer and SwapChain need these indices
    std::shared_ptr<QueueFamilyIndices> queueIndicesRef; // m_blockRegistryRef removed
    VkQueue graphicsQueueRef;
    VkQueue presentQueueRef;

    // --- Constants ---
    const int MAX_FRAMES_IN_FLIGHT = 2;

    std::unique_ptr<VulkanSwapChain> swapChainManager;
    std::unique_ptr<VulkanBufferManager> bufferManager;
    std::unique_ptr<VulkanPipelineFactory> pipelineFactory;
    std::unique_ptr<VulkanDevice> m_vulkanDeviceWrapper; // Wrapper for VkDevice/VkPhysicalDevice
    std::unique_ptr<VulkanDescriptorSetManager> descriptorSetManager;
    // std::unique_ptr<VulkanTextureLoader> textureLoader; // Replaced by ResourceManager
    std::unique_ptr<ResourceManager> resourceManager; // Manages models and textures
    std::shared_ptr<Camera> m_camera; // Store the camera
    const World* m_worldRef = nullptr; // Reference to the world data

    // --- Depth Buffer Resources ---
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat; // Store the chosen depth format
    // --- Rendering ---
    VkRenderPass renderPass = VK_NULL_HANDLE;
    // void prepareBlockTextures(uint32_t currentImage); // Removed, atlas is used
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // --- Synchronization ---
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    // --- Aggregated Chunk Mesh Buffers ---
    VkBuffer aggregatedVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory aggregatedVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer aggregatedIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory aggregatedIndexBufferMemory = VK_NULL_HANDLE;
    uint32_t totalAggregatedIndices = 0;
    // Structure to hold draw commands for each chunk if using a single aggregated buffer
    struct ChunkDrawCommand {
        uint32_t indexCount;
        uint32_t firstIndex;
        int32_t vertexOffset;
        glm::mat4 modelMatrix; // Pre-calculate model matrix for the chunk
    };
    std::vector<ChunkDrawCommand> chunkDrawCommands;

    // --- Graphics Pipeline ---
    // std::vector<std::vector<VkDescriptorSet>> descriptorSetsPerBlock; // Removed
    // uint32_t numBlocksLastFrame = 0; // Removed
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // --- Descriptors for Uniforms ---
    // These are now managed by VulkanDescriptorSetManager
    // VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    // VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    // std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped; // For persistent mapping

    // --- Setup Steps (Internal) ---
    void createRenderPass();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    // These methods are effectively replaced by calls to VulkanDescriptorSetManager
    // void createDescriptorSetLayout();
    // void createDescriptorPool();
    // void createDescriptorSets(); // Logic for vkUpdateDescriptorSets will remain in init()
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void cleanupDepthResources(); // Helper to clean up depth buffer resources

    void recreateSwapChainResources();
    void updateUniformBuffer(uint32_t currentImage);
};

#endif // VULKAN_RENDERER_HPP