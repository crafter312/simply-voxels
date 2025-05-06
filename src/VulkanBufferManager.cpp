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

void VulkanBufferManager::createIndexBuffer(const std::vector<uint16_t>& indices, VkBuffer& outIndexBuffer, VkDeviceMemory& outIndexBufferMemory) {
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