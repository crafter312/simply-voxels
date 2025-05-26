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
#include <set>      // For tracking pending mesh tasks
#include <map> // To store per-chunk render data
#include <stdexcept>
#include <string>   // For shader file loading
#include <fstream>  // For shader file loading
#include <array>    // For Vertex attributes
#include <cstdint>  // Required for uint32_t

#include <thread>   // For std::thread::hardware_concurrency()
#include <algorithm> // For std::max
#include <future>   // For std::future (asynchronous meshing)
#include "../world/ChunkMesher.hpp" // For ChunkMesher::MeshData
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
class Player; // Forward declaration for Player
class Chunk; // Forward declaration for Chunk

// --- Uniform Buffer Object ---
// class VulkanTextureLoader; // No longer directly managed here

struct UniformBufferObject {
       // glm::mat4 model; // Model matrix will be handled by push constants
    glm::mat4 view;
    glm::mat4 proj;
};

// --- Structure for Deferred Deletion ---
struct ResourceToDelete {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// Forward declare VulkanSwapChain
class VulkanSwapChain;

// Include World.hpp to get the definition of World, IVec3Comparator, and Chunk
#include "../world/World.hpp"


class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow& glfwWindow, VkInstance instance, VkSurfaceKHR surface, VulkanDevice& vulkanDevice, World& worldRef, BlockRegistry& blockRegistryRef, std::shared_ptr<Camera> cameraPtr, Player& playerRef);
    ~VulkanRenderer(); // Use destructor for cleanup

    // Call this after constructor to create pipeline resources
    void init();

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
    // MAX_REBASE_MESH_UPDATES_PER_FRAME is no longer needed as rebase only updates model matrices immediately.
    // Value determined at runtime based on hardware_concurrency.
    static const size_t MAX_CONCURRENT_MESHING_TASKS;

    std::unique_ptr<VulkanSwapChain> swapChainManager;
    std::unique_ptr<VulkanBufferManager> bufferManager;
    std::unique_ptr<VulkanPipelineFactory> pipelineFactory;
    std::unique_ptr<VulkanDescriptorSetManager> descriptorSetManager;
    // std::unique_ptr<VulkanTextureLoader> textureLoader; // Replaced by ResourceManager
    std::unique_ptr<ResourceManager> resourceManager; // Manages models and textures
    std::shared_ptr<Camera> m_camera; // Store the camera
    BlockRegistry& m_blockRegistryRef; // Reference to the block registry
    World& m_world; // Reference to the world data
    Player& m_playerRef; // Reference to the player object

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

    // For asynchronous chunk meshing
    // Store chunkCoord with the future to correctly manage m_submittedMeshTasks on exception
    std::vector<std::pair<glm::ivec3, std::future<ModelData>>> m_pendingMeshFutures;
    std::set<glm::ivec3, IVec3Comparator> m_submittedMeshTasks; // Chunks for which a mesh task has been launched
    std::vector<std::vector<ResourceToDelete>> m_deletionQueues; // Indexed by frame in flight for deferred deletion

    // Helper to destroy chunk buffers
    void destroyChunkRenderData(ChunkRenderData& data);
    void destroyAllChunkRenderData();

    // --- Graphics Pipeline ---
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // --- Wireframe Rendering Pipeline (for targeted block outline) ---
    VkPipelineLayout m_wireframePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_wireframePipeline = VK_NULL_HANDLE;
    VkBuffer m_wireframeVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_wireframeVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_wireframeIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_wireframeIndexBufferMemory = VK_NULL_HANDLE;
    uint32_t m_wireframeIndexCount = 0;
    glm::mat4 m_wireframeModelMatrix; // Model matrix to position the wireframe
    std::optional<glm::i64vec3> m_lastTargetedBlockPos; // To track if the targeted block changed
    bool m_wireframeMeshNeedsUpdate = true; // Flag to rebuild wireframe mesh


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
    // void createChunkRenderData(const glm::ivec3& chunkCoord, const Chunk& chunk); // Old synchronous version
    void createChunkRenderDataFromMeshData(const glm::ivec3& chunkCoord, const ModelData& meshData); // New version
    void updateTargetedBlockWireframe(); // New function for wireframe
};

#endif // VULKAN_RENDERER_HPP