#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string> // Though not directly used in this header, often included with vector
#include "../resource/ModelLoader.hpp" // For Vertex struct (moved from Vertex.hpp)

class VulkanBufferManager {
public:
    VulkanBufferManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue);
    ~VulkanBufferManager();

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    void createVertexBuffer(const std::vector<Vertex>& vertices,
                            VkBuffer& outVertexBuffer, VkDeviceMemory& outVertexBufferMemory);

    void createIndexBuffer(const std::vector<uint32_t>& indices, // Changed from uint16_t to uint32_t
                           VkBuffer& outIndexBuffer, VkDeviceMemory& outIndexBufferMemory);

    void createUniformBuffers(uint32_t numBuffers, VkDeviceSize bufferSize,
                              std::vector<VkBuffer>& outUniformBuffers,
                              std::vector<VkDeviceMemory>& outUniformBuffersMemory,
                              std::vector<void*>& outUniformBuffersMapped);

    // New method for creating depth buffer resources
    void createDepthResources(VkExtent2D swapChainExtent,
                              VkImage& outDepthImage,
                              VkDeviceMemory& outDepthImageMemory,
                              VkImageView& outDepthImageView,
                              VkFormat& outDepthFormat); // Output: the chosen depth format

private:
    VkDevice deviceRef;
    VkPhysicalDevice physicalDeviceRef;
    VkCommandPool commandPoolRef;
    VkQueue graphicsQueueRef;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    // Helper methods for image creation
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat(); // Helper to find a suitable depth format
    static bool hasStencilComponent(VkFormat format); // Helper to check for stencil component
};