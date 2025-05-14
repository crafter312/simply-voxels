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

    // Constructor for creating an empty texture (e.g., for a texture atlas)
    VulkanTextureLoader(VkPhysicalDevice physicalDevice,
                        VkDevice device,
                        VkCommandPool commandPool,
                        VkQueue graphicsQueue,
                        uint32_t width, uint32_t height,
                        VkFormat format, VkImageUsageFlags usage,
                        VkImageTiling tiling, VkMemoryPropertyFlags properties,
                        bool createSampler = true);
    ~VulkanTextureLoader();

    VulkanTextureLoader(const VulkanTextureLoader&) = delete;
    VulkanTextureLoader& operator=(const VulkanTextureLoader&) = delete;
    VulkanTextureLoader(VulkanTextureLoader&&) = delete;
    VulkanTextureLoader& operator=(VulkanTextureLoader&&) = delete;

    VkImageView getImageView() const { return textureImageView_; }
    VkSampler getSampler() const { return textureSampler_; }
    VkImage getImage() const { return textureImage_; }
    uint32_t getWidth() const { return texWidth_; }
    uint32_t getHeight() const { return texHeight_; }
    std::string getPath() const { return texturePath_; } // Returns path if loaded from file, empty otherwise

    // Command buffer utilities (made static and public)
    static VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
    static void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer);

private:
    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue graphicsQueue_;

    VkImage textureImage_;
    VkDeviceMemory textureImageMemory_;
    VkImageView textureImageView_;
    VkSampler textureSampler_;
    
    uint32_t texWidth_;
    uint32_t texHeight_;
    std::string texturePath_; // Store the path of the loaded texture
    VkFormat imageFormat_; // Store the format of the texture image

    uint32_t mipLevels_ = 1; // For now, we'll stick to 1 mip level

    void loadImageFromFile(const std::string& path, 
                           int& texWidth, 
                           int& texHeight, 
                           VkDeviceSize& imageSize, 
                           stbi_uc** pixels);

    void createTextureImage(const std::string& path);
    void createEmptyTextureImage(uint32_t width, uint32_t height, 
                                 /* VkFormat format, */ VkImageUsageFlags usage, // Format will be taken from member imageFormat_
                                 VkImageTiling tiling, VkMemoryPropertyFlags properties);

    // Helper functions (can remain private members or become static if they don't rely on much state)
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

    void createTextureImageView();
    void createTextureSampler();
};

#endif // VULKAN_TEXTURE_LOADER_HPP