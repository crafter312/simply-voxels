#ifndef VULKAN_TEXTURE_LOADER_HPP
#define VULKAN_TEXTURE_LOADER_HPP

#include <vulkan/vulkan.h>
#include <string>

// Forward declaration for stb_image
typedef unsigned char stbi_uc;

class VulkanTextureLoader {
public:
    VulkanTextureLoader(VkPhysicalDevice physicalDevice, 
                        VkDevice device, 
                        VkCommandPool commandPool, 
                        VkQueue graphicsQueue, 
                        const std::string& texturePath);
    ~VulkanTextureLoader();

    VulkanTextureLoader(const VulkanTextureLoader&) = delete;
    VulkanTextureLoader& operator=(const VulkanTextureLoader&) = delete;
    VulkanTextureLoader(VulkanTextureLoader&&) = delete;
    VulkanTextureLoader& operator=(VulkanTextureLoader&&) = delete;

    VkImageView getImageView() const { return textureImageView_; }
    VkSampler getSampler() const { return textureSampler_; }

private:
    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue graphicsQueue_;

    VkImage textureImage_;
    VkDeviceMemory textureImageMemory_;
    VkImageView textureImageView_;
    VkSampler textureSampler_;

    uint32_t mipLevels_ = 1; // For now, we'll stick to 1 mip level

    void loadImageFromFile(const std::string& path, 
                           int& texWidth, 
                           int& texHeight, 
                           VkDeviceSize& imageSize, 
                           stbi_uc** pixels);

    void createTextureImage(const std::string& path);
    
    void createBuffer(VkDeviceSize size, 
                      VkBufferUsageFlags usage, 
                      VkMemoryPropertyFlags properties, 
                      VkBuffer& buffer, 
                      VkDeviceMemory& bufferMemory);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void transitionImageLayout(VkImage image, 
                               VkFormat format, 
                               VkImageLayout oldLayout, 
                               VkImageLayout newLayout);

    void copyBufferToImage(VkBuffer buffer, 
                           VkImage image, 
                           uint32_t width, 
                           uint32_t height);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    void createTextureImageView();
    void createTextureSampler();
};

#endif // VULKAN_TEXTURE_LOADER_HPP