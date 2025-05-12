
#include "VulkanDescriptorSetManager.hpp"
#include "VulkanDevice.hpp" // Need definition for getLogicalDevice()
#include "VulkanSwapChain.hpp" // Need definition for getImageCount()

#include <iostream>

VulkanDescriptorSetManager::VulkanDescriptorSetManager() {}

VulkanDescriptorSetManager::~VulkanDescriptorSetManager() {
    cleanup();
}

void VulkanDescriptorSetManager::initialize(VulkanDevice* dev, VulkanSwapChain* sc) {
    if (!dev || !sc) {
        throw std::runtime_error("VulkanDescriptorSetManager requires valid device and swap chain pointers for initialization!");
    }
    device = dev;
    swapChain = sc;
}

void VulkanDescriptorSetManager::cleanup() {
    // Check if device is still valid before attempting Vulkan calls
    VkDevice logicalDevice = VK_NULL_HANDLE;
    if (device) {
        logicalDevice = device->getLogicalDevice();
    }

    if (logicalDevice != VK_NULL_HANDLE) {
        // Descriptor sets are implicitly freed when the pool is destroyed
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }

        if (descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    // Clearing the vector is good practice even if sets are freed with the pool
    descriptorSets.clear();

    // Don't destroy device or swapChain here, they are owned elsewhere.
    device = nullptr;
    swapChain = nullptr;
}

VkDevice VulkanDescriptorSetManager::getDevice() const {
    if (!device || device->getLogicalDevice() == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanDescriptorSetManager not initialized with a valid device!");
    }
    return device->getLogicalDevice();
}

void VulkanDescriptorSetManager::createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    VkDevice logicalDevice = getDevice();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void VulkanDescriptorSetManager::createDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets) {
    VkDevice logicalDevice = getDevice();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    // Optional: poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void VulkanDescriptorSetManager::createDescriptorSets() {
    VkDevice logicalDevice = getDevice();
    if (descriptorSetLayout == VK_NULL_HANDLE) {
         throw std::runtime_error("Cannot create descriptor sets: descriptor set layout is not created!");
    }
     if (descriptorPool == VK_NULL_HANDLE) {
         throw std::runtime_error("Cannot create descriptor sets: descriptor pool is not created!");
    }
    if (!swapChain) {
         throw std::runtime_error("Cannot create descriptor sets: swap chain is not initialized!");
    }

    uint32_t imageCount = swapChain->getImageCount();
    std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = imageCount;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(imageCount);
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    // Note: The actual updating of descriptor sets (vkUpdateDescriptorSets)
    // is application-specific and depends on the resources (buffers, images).
    // This part is NOT moved into the manager as it requires knowledge
    // of the specific resources being bound. The renderer (or a higher level)
    // will need to call vkUpdateDescriptorSets using the allocated sets.
}

std::optional<std::vector<VkDescriptorSet>> VulkanDescriptorSetManager::allocateDescriptorSets(uint32_t setCount) {
    if (setCount == 0) {
        return std::vector<VkDescriptorSet>(); // Return empty vector if 0 sets requested
    }

    VkDevice logicalDevice = getDevice(); // Throws if device is not valid
    if (descriptorSetLayout == VK_NULL_HANDLE) {
         std::cerr << "VulkanDescriptorSetManager Error: Cannot allocate descriptor sets: descriptor set layout is not created!" << std::endl;
         return std::nullopt;
    }
    if (descriptorPool == VK_NULL_HANDLE) {
         std::cerr << "VulkanDescriptorSetManager Error: Cannot allocate descriptor sets: descriptor pool is not created!" << std::endl;
         return std::nullopt;
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> allocatedSets(setCount);
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, allocatedSets.data()) != VK_SUCCESS) {
        std::cerr << "VulkanDescriptorSetManager Error: Failed to allocate " << setCount << " descriptor sets!" << std::endl;
        return std::nullopt;
    }
    return allocatedSets;
}