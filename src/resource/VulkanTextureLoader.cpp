#include "VulkanTextureLoader.hpp" // Use the correct relative path if needed

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h> // Make sure this is in your include path

#include <stdexcept>
#include <iostream> // For error messages
#include <cmath>     // For std::log2, std::floor, std::max
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
    // mipLevels_ will be calculated within createTextureImage after dimensions are known.
    // texWidth_ and texHeight_ will also be set there.
    createTextureImage(texturePath); // This will load image, set dimensions, and calculate mipLevels_
    createTextureImageView();        // Uses members: imageFormat_, mipLevels_
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
    // Calculate mip levels for the empty texture
    if (width == 0 || height == 0) { // Should not happen for a valid atlas
        mipLevels_ = 1;
    } else {
        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    }
    createEmptyTextureImage(width, height, usage, tiling, properties); // Uses members: texWidth_, texHeight_, imageFormat_, mipLevels_
    createTextureImageView(); // Uses members: imageFormat_, mipLevels_
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

    // Calculate mip levels now that dimensions are known
    mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth_, texHeight_)))) + 1;
    
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
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Will be transitioned
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; // SRC for atlas copy, DST for staging buffer copy, SAMPLED for rendering
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

    // Transition only mip level 0 to TRANSFER_DST_OPTIMAL for the copy
    transitionImageLayout(textureImage_, imageFormat_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1);
    copyBufferToImage(stagingBuffer, textureImage_, texWidth_, texHeight_);
    // Mip level 0 is now in TRANSFER_DST_OPTIMAL. Other mips are UNDEFINED.
    // Generate mipmaps. This will also transition all levels to SHADER_READ_ONLY_OPTIMAL.
    generateMipmaps(textureImage_, imageFormat_, texWidth_, texHeight_, mipLevels_);

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
    imageInfo.arrayLayers = 1; // Use member mipLevels_
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

void VulkanTextureLoader::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t baseMipLevel, uint32_t levelCount) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device_, commandPool_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout; // Use parameter
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.baseMipLevel = baseMipLevel; // Use parameter
    barrier.subresourceRange.levelCount = levelCount;     // Use parameter
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
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // Common for sampling
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        // Assuming the image was last used for sampling in a fragment shader
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; 
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        // Log the problematic transition for debugging
        std::cerr << "VulkanTextureLoader Error: Unsupported layout transition from " << oldLayout << " to " << newLayout 
                  << " for mip " << baseMipLevel << " count " << levelCount << std::endl;
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

    endSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer);
}

// Original transitionImageLayout now calls the new one for all mip levels
void VulkanTextureLoader::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    transitionImageLayout(image, format, oldLayout, newLayout, 0, mipLevels_);
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

void VulkanTextureLoader::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    if (mipLevels <= 1) {
        // If only one mip level, and it's in TRANSFER_DST (e.g. after initial copy from staging buffer, or atlas blit),
        // transition it to SHADER_READ_ONLY_OPTIMAL.
        // This assumes the caller has put mip 0 into TRANSFER_DST_OPTIMAL.
        transitionImageLayout(image, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
        return;
    }

    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, imageFormat, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        std::cerr << "VulkanTextureLoader Warning: Texture image format " << imageFormat 
                  << " does not support linear blitting for mipmap generation. Mipmaps may look suboptimal." << std::endl;
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device_, commandPool_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    // barrier.subresourceRange.levelCount = 1; // Will be set per mip level inside loop

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        // Transition mip level (i-1) to be a transfer source.
        // Mip 0 is initially TRANSFER_DST_OPTIMAL (from staging copy or atlas blit).
        // For i > 1, mip (i-1) was the destination of the previous blit (TRANSFER_DST_OPTIMAL).
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; // It was written to (either initial copy or previous blit)
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        // Destination mip level (i) is currently UNDEFINED. Transition to TRANSFER_DST_OPTIMAL.
        barrier.subresourceRange.baseMipLevel = i;
        // barrier.subresourceRange.levelCount = 1; // Still 1 for this operation
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Mips > 0 are initially undefined
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0; // No previous access to mip i
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // Will be written by blit

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
                       image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // Source mip i-1
                       image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // Destination mip i
                       1, &blit,
                       (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
        
        // Transition mip (i-1) from TRANSFER_SRC to SHADER_READ_ONLY, as it's no longer needed as a blit source.
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition the last mip level (mipLevels - 1), which is currently in TRANSFER_DST_OPTIMAL (from the last blit),
    // to SHADER_READ_ONLY_OPTIMAL.
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    // barrier.subresourceRange.levelCount = 1; // Still 1
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer);
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
    samplerInfo.magFilter = VK_FILTER_LINEAR; // Or NEAREST for pixelated look
    samplerInfo.minFilter = VK_FILTER_LINEAR; // Linear for smoother minification with mips
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
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // Linear for smoother mip transitions
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = (mipLevels_ > 0) ? static_cast<float>(mipLevels_ - 1) : 0.0f; // Max LOD is highest mip index

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &textureSampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}