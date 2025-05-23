#include "VulkanBufferManager.hpp"
#include <stdexcept>
#include <cstring> // For memcpy

VulkanBufferManager::VulkanBufferManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue)
    : deviceRef(device), physicalDeviceRef(physicalDevice), commandPoolRef(commandPool), graphicsQueueRef(graphicsQueue) {
    if (deviceRef == VK_NULL_HANDLE || physicalDeviceRef == VK_NULL_HANDLE || commandPoolRef == VK_NULL_HANDLE || graphicsQueueRef == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanBufferManager received null handles during construction!");
    }
}

VulkanBufferManager::~VulkanBufferManager() {
    // This manager doesn't own the buffers/memory it creates for VulkanRenderer,
    // so VulkanRenderer is responsible for their destruction.
}

uint32_t VulkanBufferManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDeviceRef, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanBufferManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(deviceRef, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(deviceRef, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(deviceRef, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }
    vkBindBufferMemory(deviceRef, buffer, bufferMemory, 0);
}

VkCommandBuffer VulkanBufferManager::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPoolRef;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(deviceRef, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanBufferManager::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueueRef, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueueRef);

    vkFreeCommandBuffers(deviceRef, commandPoolRef, 1, &commandBuffer);
}

void VulkanBufferManager::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

void VulkanBufferManager::createVertexBuffer(const std::vector<Vertex>& vertices, VkBuffer& outVertexBuffer, VkDeviceMemory& outVertexBufferMemory) {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(deviceRef, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(deviceRef, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outVertexBuffer, outVertexBufferMemory);
    copyBuffer(stagingBuffer, outVertexBuffer, bufferSize);

    vkDestroyBuffer(deviceRef, stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, stagingBufferMemory, nullptr);
}

void VulkanBufferManager::createVertexBuffer(
    const std::vector<WireframeMesher::WireframeVertex>& vertices,
    VkBuffer& outVertexBuffer, VkDeviceMemory& outVertexBufferMemory) {

    if (vertices.empty()) {
        // Handle empty vertex list: either throw, log, or create a dummy buffer.
        // For now, let's just return and leave the output buffers as VK_NULL_HANDLE.
        outVertexBuffer = VK_NULL_HANDLE;
        outVertexBufferMemory = VK_NULL_HANDLE;
        // std::cout << "Warning: Attempted to create a vertex buffer with no wireframe vertices." << std::endl;
        return;
    }

    VkDeviceSize bufferSize = sizeof(WireframeMesher::WireframeVertex) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(deviceRef, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(deviceRef, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outVertexBuffer, outVertexBufferMemory);
    copyBuffer(stagingBuffer, outVertexBuffer, bufferSize);

    vkDestroyBuffer(deviceRef, stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, stagingBufferMemory, nullptr);
}

void VulkanBufferManager::createIndexBuffer(const std::vector<uint32_t>& indices, VkBuffer& outIndexBuffer, VkDeviceMemory& outIndexBufferMemory) {
    if (indices.empty()) {
        throw std::runtime_error("Cannot create index buffer from empty indices vector.");
    }
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(deviceRef, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(deviceRef, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outIndexBuffer, outIndexBufferMemory);
    copyBuffer(stagingBuffer, outIndexBuffer, bufferSize);

    vkDestroyBuffer(deviceRef, stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, stagingBufferMemory, nullptr);
}

void VulkanBufferManager::createUniformBuffers(uint32_t numBuffers, VkDeviceSize bufferSize, std::vector<VkBuffer>& outUniformBuffers, std::vector<VkDeviceMemory>& outUniformBuffersMemory, std::vector<void*>& outUniformBuffersMapped) {
    outUniformBuffers.resize(numBuffers);
    outUniformBuffersMemory.resize(numBuffers);
    outUniformBuffersMapped.resize(numBuffers);

    for (uint32_t i = 0; i < numBuffers; ++i) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, outUniformBuffers[i], outUniformBuffersMemory[i]);
        vkMapMemory(deviceRef, outUniformBuffersMemory[i], 0, bufferSize, 0, &outUniformBuffersMapped[i]);
    }
}

VkFormat VulkanBufferManager::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDeviceRef, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported format!");
}

VkFormat VulkanBufferManager::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool VulkanBufferManager::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void VulkanBufferManager::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                     VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Or VK_IMAGE_LAYOUT_PREINITIALIZED if data is uploaded
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // No MSAA for the depth image itself for now
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(deviceRef, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(deviceRef, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(deviceRef, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(deviceRef, image, imageMemory, 0);
}

VkImageView VulkanBufferManager::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(deviceRef, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view for depth/texture!");
    }
    return imageView;
}

void VulkanBufferManager::createDepthResources(VkExtent2D swapChainExtent, VkImage& outDepthImage, VkDeviceMemory& outDepthImageMemory, VkImageView& outDepthImageView, VkFormat& outDepthFormat) {
    outDepthFormat = findDepthFormat();
    createImage(swapChainExtent.width, swapChainExtent.height, outDepthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outDepthImage, outDepthImageMemory);
    outDepthImageView = createImageView(outDepthImage, outDepthFormat, hasStencilComponent(outDepthFormat) ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT);
}