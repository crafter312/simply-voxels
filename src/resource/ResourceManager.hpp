#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set> // For std::set in buildTextureAtlas implementation details
#include <vulkan/vulkan.h> // For Vulkan types needed by VulkanTextureLoader

#include "ModelLoader.hpp"         // For ModelData
#include "VulkanTextureLoader.hpp" // For VulkanTextureLoader
#include "../block/Block.hpp"            // For the Block class (defines type, model/texture paths)
#include "../block/BlockRegistry.hpp"    // For BlockRegistry class

// It's assumed glm/glm.hpp or equivalent is included for glm::vec2
struct ResolvedBlockAssets {
    std::shared_ptr<const ModelData> modelData = nullptr; // Model data is typically read-only after loading
    std::shared_ptr<VulkanTextureLoader> textureLoader = nullptr;    std::string texturePathKey; // The actual path (original or default) used for this block's texture
    // You could add other frequently accessed block properties here if needed (e.g., isSolid)
};

// Information about a texture's location and scale within the atlas
struct AtlasTextureInfo {
    glm::vec2 uvOffset{0.0f, 0.0f}; // Top-left corner in atlas (0-1 range)
    glm::vec2 uvScale{1.0f, 1.0f};  // Size in atlas (subTextureSize / atlasSize)
};

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

    // Builds the texture atlas from all loaded unique block textures.
    // Should be called after all individual textures are loaded (e.g., at the end of loadAssetsFromRegistry).
    void buildTextureAtlas(const BlockRegistry& registry, uint32_t textureSize = 16);

    std::shared_ptr<const ModelData> getModelForBlockType(uint16_t blockID) const;
    std::shared_ptr<VulkanTextureLoader> getTextureForBlockType(uint16_t blockID) const;

    void setDefaultModelPath(const std::string& path);
    void setDefaultTexturePath(const std::string& path);

    // Atlas accessors
    VkImageView getAtlasImageView() const;
    VkSampler getAtlasSampler() const;
    AtlasTextureInfo getBlockAtlasInfo(uint16_t blockID) const;
    AtlasTextureInfo getTextureAtlasInfo(const std::string& texturePath) const; // For getting info by original path

private:
    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue graphicsQueue_;

    mutable std::map<std::string, std::shared_ptr<ModelData>> loadedModels_; // Cache for CPU-side model data
    mutable std::map<std::string, std::shared_ptr<VulkanTextureLoader>> loadedTextures_; // Cache for textures
    std::map<uint16_t, ResolvedBlockAssets> m_resolvedBlockAssets; // Stores resolved asset pointers keyed by block ID

    std::string defaultModelPath_;
    std::string defaultTexturePath_;

    std::shared_ptr<ModelData> internalLoadModel(const std::string& path, bool isFallbackAttempt = false) const;
    std::shared_ptr<VulkanTextureLoader> internalLoadTexture(const std::string& path, bool isFallbackAttempt = false) const;

    // Texture Atlas specific members
    std::unique_ptr<VulkanTextureLoader> m_textureAtlas; // Will hold the VkImage, VkImageView, VkSampler for the atlas
                                                         // Assumes VulkanTextureLoader can be constructed for an empty, writable texture
    std::map<std::string, AtlasTextureInfo> m_texturePathToAtlasInfoMap; // Maps original texture path to its atlas UV info
                                                                         // The key is the path used for loading (could be default path)
    AtlasTextureInfo m_defaultAtlasTextureInfo; // Atlas info for the default/error texture
};

#endif // RESOURCE_MANAGER_HPP