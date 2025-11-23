#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string> // Though not directly used in this header, often included with vector
#include "../resource/ModelLoader.hpp" // For Vertex struct (moved from Vertex.hpp)
#include "../physics/WireframeMesher.hpp" // For WireframeVertex struct

class VulkanBufferManager {
public:
    VulkanBufferManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue);
    ~VulkanBufferManager();

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    // Modified to take an external command buffer
    void copyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size);

    VkBuffer createVertexBuffer(VkCommandBuffer& commandBuffer, const std::vector<Vertex>& vertices,
                                VkDeviceSize& outVertexOffset);

    // Overload for WireframeVertex data
    VkBuffer createVertexBuffer(VkCommandBuffer& commandBuffer, const std::vector<WireframeMesher::WireframeVertex>& vertices,
                                VkDeviceSize& outVertexOffset);

    VkBuffer createIndexBuffer(VkCommandBuffer& commandBuffer, const std::vector<uint32_t>& indices,
                               VkDeviceSize& outIndexOffset);

    void createUniformBuffers(uint32_t numBuffers, VkDeviceSize bufferSize,
                              std::vector<VkBuffer>& outUniformBuffers,
                              std::vector<VkDeviceMemory>& outUniformBuffersMemory,
                              std::vector<void*>& outUniformBuffersMapped);

    // New methods for freeing sub-allocated regions
    void freeVertexBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);
    void freeIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

    // New method for creating depth buffer resources
    void createDepthResources(VkExtent2D swapChainExtent,
                              VkImage& outDepthImage,
                              VkDeviceMemory& outDepthImageMemory,
                              VkImageView& outDepthImageView,
                              VkFormat& outDepthFormat); // Output: the chosen depth format

    // --- New methods for managing a transfer command buffer ---
    VkCommandBuffer beginTransferCommands();
    void endAndSubmitTransferCommands(VkCommandBuffer commandBuffer);
    void waitForTransfersToFinish();


private:
    VkDevice deviceRef;
    VkPhysicalDevice physicalDeviceRef;
    VkCommandPool commandPoolRef;
    VkQueue graphicsQueueRef;

    // --- Reusable Staging Buffer ---
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize m_stagingBufferSize = 0;
    void* m_stagingBufferMapped = nullptr;
    VkDeviceSize m_stagingBufferCurrentOffset = 0; // New: Tracks current position in staging buffer for batching

    VkFence m_transferFence = VK_NULL_HANDLE; // Fence to manage staging buffer reuse for transfers

    // --- New members for sub-allocation ---

    // A simple struct to track free regions in a ManagedBuffer
    struct FreeBlock {
        VkDeviceSize offset;
        VkDeviceSize size;
    };

    // Represents a large, managed GPU buffer from which smaller allocations are made.
    struct ManagedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize totalSize = 0;
        VkBufferUsageFlags usage = 0;
        VkDeviceSize currentOffset = 0; // For bump allocation
        std::vector<FreeBlock> freeList; // For freed block reuse
        // Debug: track active allocations (offset/size) to detect overlaps in debug runs
        std::vector<FreeBlock> activeAllocations;
    };

    std::vector<ManagedBuffer> m_vertexBufferPool;
    std::vector<ManagedBuffer> m_indexBufferPool;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    // VkCommandBuffer beginSingleTimeCommands(); // Replaced by beginTransferCommands
    // void endSingleTimeCommands(VkCommandBuffer commandBuffer); // Replaced by endAndSubmitTransferCommands

    VkBuffer allocateBufferRegion(VkDeviceSize size, VkDeviceSize& outOffset, std::vector<ManagedBuffer>& pool, VkBufferUsageFlags usage);
    void createNewManagedBuffer(VkDeviceSize size, std::vector<ManagedBuffer>& pool, VkBufferUsageFlags usage);
    void freeBufferRegion(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, std::vector<ManagedBuffer>& pool);
    void resizeStagingBuffer(VkDeviceSize newSize); // Will be made private in implementation but kept here for diff clarity

    // Helper methods for image creation
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat(); // Helper to find a suitable depth format
    static bool hasStencilComponent(VkFormat format); // Helper to check for stencil component
};