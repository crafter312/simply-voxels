#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS // Ensure GLM uses radians
#include <glm/glm.hpp>    // For vec3, mat4
#include <glm/gtc/matrix_transform.hpp> // For lookAt, perspective, etc.

#include <vector>
#include <memory>   // For std::unique_ptr
#include <optional>
#include <stdexcept>
#include <string>   // For shader file loading
#include <fstream>  // For shader file loading
#include <array>    // For Vertex attributes
#include <cstdint> // Required for uint32_t

// Forward declare HelloVulkanApp types needed here
struct QueueFamilyIndices;

// --- Vertex Data ---

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color; // Add color attribute

    // Describe how to pass this vertex data to the vertex shader
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0; // Index of the binding in the array of bindings
        bindingDescription.stride = sizeof(Vertex); // Distance in bytes between consecutive elements
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to the next data entry after each vertex

        return bindingDescription;
    }

    // Describe how to handle vertex input attribute (position)
    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescription() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        // Position attribute
        attributeDescriptions[0].binding = 0; // Which binding the per-vertex data comes from
        attributeDescriptions[0].location = 0; // Corresponds to 'location = 0' in the vertex shader
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // Format of the data (vec3)
        attributeDescriptions[0].offset = offsetof(Vertex, pos); // Byte offset of this attribute within the struct

        // Color attribute
        attributeDescriptions[1].binding = 0; // Data comes from the same binding
        attributeDescriptions[1].location = 1; // Corresponds to 'location = 1' in the vertex shader
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // Format of the data (vec3)
        attributeDescriptions[1].offset = offsetof(Vertex, color); // Byte offset of this attribute

        return attributeDescriptions;
    }
};

// Define the vertices and indices for a cube
// We'll define these as constants directly in the header for simplicity,
// though for larger models, you'd load them from a file.
const std::vector<Vertex> cubeVertices = {
    // Front face (Z+) Red
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 3
    // Back face (Z-) Cyan
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 4
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 5
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 6
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 7
    // Left face (X-) Green
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}}, // 8 (orig 4)
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}}, // 9 (orig 0)
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}}, // 10 (orig 3)
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}}, // 11 (orig 7)
    // Right face (X+) Magenta
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}}, // 12 (orig 1)
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 13 (orig 5)
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 14 (orig 6)
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}}, // 15 (orig 2)
    // Top face (Y+) Blue
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}, // 16 (orig 3)
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}, // 17 (orig 2)
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}}, // 18 (orig 6)
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}}, // 19 (orig 7)
    // Bottom face (Y-) Yellow
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}}, // 20 (orig 4)
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}}, // 21 (orig 5)
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}}, // 22 (orig 1)
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}}  // 23 (orig 0)
};

// Indices define the triangles that make up the cube faces
// Using uint16_t is fine for simple models like a cube (< 65536 vertices)
// Now indices reference the duplicated vertices above. 6 faces * 2 triangles/face * 3 vertices/triangle = 36 indices
const std::vector<uint16_t> cubeIndices = {
     0,  1,  2,   2,  3,  0, // Front
     4,  5,  6,   6,  7,  4, // Back
     8,  9, 10,  10, 11,  8, // Left
    12, 13, 14,  14, 15, 12, // Right
    16, 17, 18,  18, 19, 16, // Top
    20, 21, 22,  22, 23, 20  // Bottom
};

// --- Uniform Buffer Object ---

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

// Forward declare VulkanSwapChain
class VulkanSwapChain;

class VulkanRenderer {
public:
    // Constructor takes necessary handles and pre-queried support details
    VulkanRenderer(GLFWwindow* glfwWindow, VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice logicalDevice, QueueFamilyIndices queueIndices, VkQueue graphicsQueueHandle, VkQueue presentQueueHandle);
    ~VulkanRenderer(); // Use destructor for cleanup

    // Call this after constructor to create pipeline resources
    void init();

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
    std::shared_ptr<QueueFamilyIndices> queueIndicesRef;
    VkQueue graphicsQueueRef;
    VkQueue presentQueueRef;

    // --- Constants ---
    const int MAX_FRAMES_IN_FLIGHT = 2;

    std::unique_ptr<VulkanSwapChain> swapChainManager;

    // --- Rendering ---
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // --- Synchronization ---
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    // --- Vertex/Index Buffers ---
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;

    // --- Graphics Pipeline ---
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // --- Descriptors for Uniforms ---
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped; // For persistent mapping

    // --- Setup Steps (Internal) ---
    void createRenderPass();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createDescriptorSetLayout();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createVertexBuffer();
    void createGraphicsPipeline();
    void createIndexBuffer();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // Renamed from recreateSwapChain to reflect it recreates more now
    void recreateSwapChainResources();

    void updateUniformBuffer(uint32_t currentImage);
    // --- Helpers (Internal) ---
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename); // Static helper
};

#endif // VULKAN_RENDERER_HPP