
#pragma once

#include "VulkanCommon.hpp"

#include <vector>
#include <stdexcept> // For exceptions
#include <optional>  // For std::optional

// Forward declarations
class VulkanDevice;
class VulkanSwapChain;

class VulkanDescriptorSetManager {
public:
    VulkanDescriptorSetManager();
    ~VulkanDescriptorSetManager();

    // Disable copy and assignment
    VulkanDescriptorSetManager(const VulkanDescriptorSetManager&) = delete;
    VulkanDescriptorSetManager& operator=(const VulkanDescriptorSetManager&) = delete;

    // Initialize with dependencies
    void initialize(VulkanDevice* device, VulkanSwapChain* swapChain);

    // Create the layout based on provided bindings
    // IMPORTANT: You will need to pass the actual bindings from your original createDescriptorSetLayout here.
    void createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);

    // Create the pool based on provided sizes and max sets
    // IMPORTANT: You will need to pass the actual pool sizes and max sets from your original createDescriptorPool here.
    void createDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets);

    // Allocate descriptor sets from the pool using the layout
    // Uses the stored layout, pool, and swap chain image count
    void createDescriptorSets();

    // Allocate a specific number of descriptor sets
    std::optional<std::vector<VkDescriptorSet>> allocateDescriptorSets(uint32_t setCount);

    // Getters
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
    VkDescriptorPool getImguiDescriptorPool() const { return m_imguiDescriptorPool; }

    // --- ImGui Specific ---
    void createImguiDescriptorPool();

    // Cleanup method (called by destructor)
    void cleanup();

private:
    VulkanDevice* device = nullptr;
    VulkanSwapChain* swapChain = nullptr; // Needed for image count

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Internal helper to get device handle safely
    VkDevice getDevice() const;
};