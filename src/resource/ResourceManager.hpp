#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <vulkan/vulkan.h> // For Vulkan types needed by VulkanTextureLoader

#include "ModelLoader.hpp"         // For ModelData
#include "VulkanTextureLoader.hpp" // For VulkanTextureLoader
#include "../Block.hpp"            // For the Block class (defines type, model/texture paths)
#include "../BlockRegistry.hpp"    // For BlockRegistry class

class ResourceManager {
public:
    ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue);
    ~ResourceManager();

    // Delete copy/move constructors and assignment operators
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    // Processes block definitions from the registry, stores their asset paths, and optionally pre-loads assets.
    void loadAssetsFromRegistry(const BlockRegistry& registry, bool preLoadAll = true);
    
    const ModelData& getModelForBlockType(const std::string& typeName);
    VulkanTextureLoader* getTextureForBlockType(const std::string& typeName);

    void setDefaultModelPath(const std::string& path);
    void setDefaultTexturePath(const std::string& path);

private:
    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue graphicsQueue_;

    std::map<std::string, ModelData> loadedModels_; // Cache for CPU-side model data
    std::map<std::string, std::unique_ptr<VulkanTextureLoader>> loadedTextures_; // Cache for textures (which manage their own GPU resources)
    std::map<std::string, Block> blockAssetPaths_; // Stores Block objects (containing paths) from the registry, keyed by type name

    std::string defaultModelPath_;
    std::string defaultTexturePath_;

    const ModelData& internalLoadModel(const std::string& path, bool isFallbackAttempt = false);
    VulkanTextureLoader* internalLoadTexture(const std::string& path, bool isFallbackAttempt = false);
};

#endif // RESOURCE_MANAGER_HPP