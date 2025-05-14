#include "VulkanTextureLoader.hpp" // Use the correct relative path if needed

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h> // Make sure this is in your include path

#include <stdexcept>
#include <iostream> // For error messages
#include <algorithm> // For std::max

VulkanTextureLoader::VulkanTextureLoader(VkPhysicalDevice physicalDevice, 
                                         VkDevice device, 
                                         VkCommandPool commandPool, 
                                         VkQueue graphicsQueue, 
                                         const std::string& texturePath)
    : physicalDevice_(physicalDevice), 
      device_(device), 
      commandPool_(commandPool), 
      graphicsQueue_(graphicsQueue),
      texturePath_(texturePath), // Store path
      imageFormat_(VK_FORMAT_R8G8B8A8_SRGB) { // Default format for loaded images
    // texWidth_ and texHeight_ will be set by createTextureImage
    createTextureImage(texturePath); // This will use imageFormat_
    createTextureImageView();        // This will use imageFormat_
    createTextureSampler();
}

// Constructor for creating an empty texture (e.g., for a texture atlas)
VulkanTextureLoader::VulkanTextureLoader(VkPhysicalDevice physicalDevice,
                                         VkDevice device,
                                         VkCommandPool commandPool,
                                         VkQueue graphicsQueue,
                                         uint32_t width, uint32_t height,
                                         VkFormat format, VkImageUsageFlags usage,
                                         VkImageTiling tiling, VkMemoryPropertyFlags properties,
                                         bool createSampler)
    : physicalDevice_(physicalDevice),
      device_(device),
      commandPool_(commandPool),
      graphicsQueue_(graphicsQueue),
      texWidth_(width),
      texHeight_(height),
      texturePath_(""), // No path for empty texture
      imageFormat_(format) { // Use provided format
    createEmptyTextureImage(width, height, /* format, */ usage, tiling, properties);
    createTextureImageView(); // This will use imageFormat_
    if (createSampler) {
        createTextureSampler();
    } else {
        textureSampler_ = VK_NULL_HANDLE; // Explicitly null if not created
    }
}

VulkanTextureLoader::~VulkanTextureLoader() {
    vkDestroySampler(device_, textureSampler_, nullptr);
    vkDestroyImageView(device_, textureImageView_, nullptr);
    vkDestroyImage(device_, textureImage_, nullptr);
    vkFreeMemory(device_, textureImageMemory_, nullptr);
}

void VulkanTextureLoader::loadImageFromFile(const std::string& path, 
                                            int& texWidth, 
                                            int& texHeight, 
                                            VkDeviceSize& imageSize, 
                                            stbi_uc** pixels) {
    int texChannels;
    *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!*pixels) {
        throw std::runtime_error("Failed to load texture image: " + path);
    }
    imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4; // 4 bytes per pixel (RGBA)
}

void VulkanTextureLoader::createTextureImage(const std::string& path) {
    int localTexWidth, localTexHeight; // Use local vars to pass to loadImageFromFile
    VkDeviceSize imageSize;
    stbi_uc* pixels;

    loadImageFromFile(path, localTexWidth, localTexHeight, imageSize, &pixels);

    texWidth_ = static_cast<uint32_t>(localTexWidth);   // Store member variable
    texHeight_ = static_cast<uint32_t>(localTexHeight); // Store member variable
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, 
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                 stagingBuffer, 
                 stagingBufferMemory);

    void* data;
    vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device_, stagingBufferMemory);

    stbi_image_free(pixels); // Free CPU-side image data

    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth_; // Use member variable
    imageInfo.extent.height = texHeight_; // Use member variable
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 1;
    imageInfo.format = imageFormat_; // Use member imageFormat_
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0; // Optional

    if (vkCreateImage(device_, &imageInfo, nullptr, &textureImage_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, textureImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &textureImageMemory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(device_, textureImage_, textureImageMemory_, 0);

    // Transition layout and copy buffer
    transitionImageLayout(textureImage_, imageFormat_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, textureImage_, texWidth_, texHeight_);
    transitionImageLayout(textureImage_, imageFormat_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cleanup staging buffer
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingBufferMemory, nullptr);
}

void VulkanTextureLoader::createEmptyTextureImage(uint32_t width, uint32_t height,
                                                 /* VkFormat format, */ VkImageUsageFlags usage,
                                                 VkImageTiling tiling, VkMemoryPropertyFlags properties) {
    // texWidth_, texHeight_, and imageFormat_ are already set by the constructor.

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth_; // Use member texWidth_
    imageInfo.extent.height = texHeight_; // Use member texHeight_
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_; // Typically 1 for an atlas
    imageInfo.arrayLayers = 1;
    imageInfo.format = imageFormat_;    // Use member imageFormat_
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Atlas will be transitioned by ResourceManager
    imageInfo.usage = usage; // e.g., VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    if (vkCreateImage(device_, &imageInfo, nullptr, &textureImage_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create empty image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, textureImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &textureImageMemory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate empty image memory!");
    }

    vkBindImageMemory(device_, textureImage_, textureImageMemory_, 0);
    // The image is created and memory bound. It's in VK_IMAGE_LAYOUT_UNDEFINED.
    // ResourceManager will be responsible for transitioning layout and copying data into it.
}

uint32_t VulkanTextureLoader::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanTextureLoader::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device_, buffer, bufferMemory, 0);
}

VkCommandBuffer VulkanTextureLoader::beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool; // Use parameter
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer); // Use parameter

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo); // Use parameter
    return commandBuffer;
}

void VulkanTextureLoader::endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    // Use parameters
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue); // Wait for the transfer to complete

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanTextureLoader::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = VulkanTextureLoader::beginSingleTimeCommands(device_, commandPool_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels_;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VulkanTextureLoader::endSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer);
}

void VulkanTextureLoader::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = VulkanTextureLoader::beginSingleTimeCommands(device_, commandPool_);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VulkanTextureLoader::endSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer);
}

void VulkanTextureLoader::createTextureImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // Assuming 2D textures
    viewInfo.format = imageFormat_; // Use the stored format
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &textureImageView_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }
}

void VulkanTextureLoader::createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; // Change for sharp magnification
    samplerInfo.minFilter = VK_FILTER_NEAREST; // Change for sharp minification
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);

    // Only enable anisotropy if the physical device supports it (maxAnisotropy > 1.0)
    // AND if the feature was requested and enabled on the logical device (handled by HelloVulkanApp)
    // The validation error occurs if the logical device feature is off.
    // Here, we ensure we only *try* to use it if the physical device has any level of support.
    if (properties.limits.maxSamplerAnisotropy > 1.0f) {
        samplerInfo.anisotropyEnable = VK_TRUE; 
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    } else {
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // Also change if you implement mipmaps later
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels_); // For mipLevels_ = 1, this doesn't have much effect yet

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &textureSampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}