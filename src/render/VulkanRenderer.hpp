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
#include <map> // To store per-chunk render data
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
//#include "../world/World.hpp" // This will bring in IVec3Comparator and Chunk
class Chunk; // Forward declaration for Chunk

// --- Uniform Buffer Object ---
// class VulkanTextureLoader; // No longer directly managed here

struct UniformBufferObject {
       // glm::mat4 model; // Model matrix will be handled by push constants
    glm::mat4 view;
    glm::mat4 proj;
};

// Forward declare VulkanSwapChain
class VulkanSwapChain;

// Include World.hpp to get the definition of World, IVec3Comparator, and Chunk
#include "../world/World.hpp"


class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow& glfwWindow, VkInstance instance, VkSurfaceKHR surface, VulkanDevice& vulkanDevice, World& worldRef, std::shared_ptr<Camera> cameraPtr);
    ~VulkanRenderer(); // Use destructor for cleanup

    // Call this after constructor to create pipeline resources
    void init(const BlockRegistry& blockRegistryRef);

    // Main drawing function
    void drawFrame();

    // Public flag to signal resize from callback
    bool framebufferResized = false;

private:
    // --- References to external objects ---
    GLFWwindow& window; // Changed from GLFWwindow* to GLFWwindow&
    VkInstance instanceRef; // Keep refs to objects managed by HelloVulkanApp
    VkSurfaceKHR surfaceRef;
    VulkanDevice& m_vulkanDeviceRef; // Reference to the main VulkanDevice object

    // --- Constants ---
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    static constexpr int MAX_REBASE_MESH_UPDATES_PER_FRAME = 5; // Limit how many chunk meshes are rebuilt per frame after rebase
    std::unique_ptr<VulkanSwapChain> swapChainManager;
    std::unique_ptr<VulkanBufferManager> bufferManager;
    std::unique_ptr<VulkanPipelineFactory> pipelineFactory;
    std::unique_ptr<VulkanDescriptorSetManager> descriptorSetManager;
    // std::unique_ptr<VulkanTextureLoader> textureLoader; // Replaced by ResourceManager
    std::unique_ptr<ResourceManager> resourceManager; // Manages models and textures
    std::shared_ptr<Camera> m_camera; // Store the camera
    World& m_world; // Reference to the world data

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
    // std::vector<VkSemaphore> renderFinishedSemaphores; // Replaced by presentationFinishedSemaphores for clarity and robustness
    std::vector<VkFence> inFlightFences;
    std::vector<VkSemaphore> presentationFinishedSemaphores; // Semaphores signaled when rendering to an image is finished, waited on by presentation
    std::vector<VkFence> imagesInFlight; // Tracks fences for each swap chain image
    uint32_t currentFrame = 0;

    // --- Aggregated Chunk Mesh Buffers ---
    struct ChunkRenderData {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount;
        // No need for firstIndex or vertexOffset with per-chunk buffers
        glm::mat4 modelMatrix; // Model matrix for this chunk
    };
    std::map<glm::ivec3, ChunkRenderData, struct IVec3Comparator> m_chunkRenderData; // Map chunk coordinates to render data

    // Helper to destroy chunk buffers
    void destroyChunkRenderData(ChunkRenderData& data);
    void destroyAllChunkRenderData();

    // --- Graphics Pipeline ---
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
    // Renamed and modified to process changes
    void processChunkChanges();
    void createChunkRenderData(const glm::ivec3& chunkCoord, const Chunk& chunk);
};

#endif // VULKAN_RENDERER_HPP