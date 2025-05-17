#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set> // For std::set in buildTextureAtlas implementation details
#include <vulkan/vulkan.h> // For Vulkan types needed by VulkanTextureLoader
#include <mutex>        // For std::unique_lock
#include <shared_mutex> // For std::shared_mutex

#include "ModelLoader.hpp"         // For ModelData
#include "VulkanTextureLoader.hpp" // For VulkanTextureLoader
#include "../block/Block.hpp"            // For the Block class (defines type, model/texture paths)
#include "../block/BlockRegistry.hpp"    // For BlockRegistry class

// It's assumed glm/glm.hpp or equivalent is included for glm::vec2
struct ResolvedBlockAssets {
    // std::shared_ptr<const ModelData> modelData = nullptr; // Old: raw model data
    std::shared_ptr<const SeparableModelData> separableModelData = nullptr; // New: processed model data
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
    void loadAssetsFromRegistry(BlockRegistry& registry, bool preLoadAll = true); // Now takes non-const BlockRegistry

    // Builds the texture atlas from all loaded unique block textures.
    // Should be called after all individual textures are loaded (e.g., at the end of loadAssetsFromRegistry).
    void buildTextureAtlas(const BlockRegistry& registry);

    std::shared_ptr<const SeparableModelData> getModelForBlockType(uint16_t blockID) const;
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
    VkQueue graphicsQueue_; // Added this member

    mutable std::map<std::string, std::shared_ptr<ModelData>> loadedModels_; // Cache, mutable for const methods
    mutable std::map<std::string, std::shared_ptr<VulkanTextureLoader>> loadedTextures_; // Cache, mutable for const methods
    mutable std::shared_mutex m_cache_mutex; // Mutex to protect loadedModels_ and loadedTextures_
    std::map<uint16_t, ResolvedBlockAssets> m_resolvedBlockAssets; // Stores resolved asset pointers keyed by block ID

    std::string defaultModelPath_;
    std::string defaultTexturePath_;

    std::shared_ptr<ModelData> internalLoadModel(const std::string& path, bool isFallbackAttempt = false) const;
    std::shared_ptr<VulkanTextureLoader> internalLoadTexture(const std::string& path, bool isFallbackAttempt = false) const;

    // Helper to analyze a model and set block properties
    std::shared_ptr<SeparableModelData> analyzeModelAndSetProperties(Block& blockDef, const ModelData& rawModelData) const;

    // Constants for model analysis
    static constexpr float MODEL_ANALYSIS_EPSILON = 1e-4f;
    static constexpr float MODEL_ANALYSIS_MIN_EXTENT = 0.0f; // Changed for 0-1 range
    static constexpr float MODEL_ANALYSIS_MAX_EXTENT = 1.0f; // Changed for 0-1 range
    static constexpr int32_t ATLAS_PADDING = 2; // Padding around each texture in the atlas
    // This factor, when multiplied by contentTileSize, gives the number of *atlas pixels*
    // to use for the UV inset on each side. E.g., 0.005f means 0.5% of contentTileSize.
    static constexpr float ATLAS_UV_INSET_FACTOR_OF_CONTENT = 0.005f; // e.g., 0.5% of content size for inset
    static constexpr uint32_t ATLAS_CLAMP_LOWEST_MIP_LEVELS = 1; // Number of lowest mip levels to avoid sampling

    // Texture Atlas specific members
    std::unique_ptr<VulkanTextureLoader> m_textureAtlas; // Will hold the VkImage, VkImageView, VkSampler for the atlas
                                                         // Assumes VulkanTextureLoader can be constructed for an empty, writable texture
    std::map<std::string, AtlasTextureInfo> m_texturePathToAtlasInfoMap; // Maps original texture path to its atlas UV info
                                                                         // The key is the path used for loading (could be default path)
    AtlasTextureInfo m_defaultAtlasTextureInfo; // Atlas info for the default/error texture
    VkSampler m_customAtlasSampler = VK_NULL_HANDLE; // Custom sampler for the atlas with LOD clamping
};

#endif // RESOURCE_MANAGER_HPP