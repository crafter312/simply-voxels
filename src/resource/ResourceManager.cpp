#include "ResourceManager.hpp"
#include <iostream> // For error messages/logging
#include <stdexcept> // For std::runtime_error
#include <set>      // For std::set to collect unique texture paths
#include <cmath>    // For ceil/sqrt in atlas dimension calculation
#include <algorithm> // For std::max
// Block.hpp and BlockRegistry.hpp are included via ResourceManager.hpp
// #include "../Block.hpp"
// #include "../BlockRegistry.hpp"

ResourceManager::ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue)
    : physicalDevice_(physicalDevice), device_(device), commandPool_(commandPool), graphicsQueue_(graphicsQueue),
      defaultModelPath_(""), defaultTexturePath_("") {
    if (physicalDevice_ == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE || commandPool_ == VK_NULL_HANDLE || graphicsQueue_ == VK_NULL_HANDLE) {
        throw std::runtime_error("ResourceManager received null Vulkan handles during construction!");
    }
    std::cout << "ResourceManager created." << std::endl;
}

ResourceManager::~ResourceManager() {
    // loadedTextures_ unique_ptrs will automatically clean up VulkanTextureLoader instances
    // m_textureAtlas unique_ptr will automatically clean up its VulkanTextureLoader instance
    // (which in turn cleans up its Vulkan resources)
    // loadedModels_ will clean up ModelData instances (which are just CPU data)
    std::cout << "ResourceManager destroyed." << std::endl;
}

void ResourceManager::loadAssetsFromRegistry(const BlockRegistry& registry, bool preLoadAll) {
    std::cout << "ResourceManager: Loading assets from block registry..." << std::endl;
    const auto& allDefinitions = registry.getAllBlockDefinitions();
    m_resolvedBlockAssets.clear(); // Clear any previously resolved assets

    for (const auto& pair : allDefinitions) {
        const Block& blockDef = pair.second; // blockDef is a Block object from BlockRegistry
        uint16_t blockID = blockDef.getID(); // Assumes Block class has getID()
        
        ResolvedBlockAssets resolved;

        // Resolve model
        // The internalLoadModel/Texture calls will load from disk if not already cached,
        // effectively handling the preLoadAll intention.
        std::string modelPathToLoad = !blockDef.getModelPath().empty() ? blockDef.getModelPath() : defaultModelPath_;
        if (!modelPathToLoad.empty()) {
            resolved.modelData = internalLoadModel(modelPathToLoad);
        } else if (blockDef.getModelPath().empty()) { // Only log if specific block had no path and no default
            std::cout << "ResourceManager: Block ID " << blockID << " has no model path, using default: " << defaultModelPath_ << std::endl;
        }

        // Resolve texture
        std::string texturePathToLoad = !blockDef.getTexturePath().empty() ? blockDef.getTexturePath() : defaultTexturePath_;
        if (!texturePathToLoad.empty()) {
            resolved.textureLoader = internalLoadTexture(texturePathToLoad); // This loads or gets from cache
            resolved.texturePathKey = texturePathToLoad; // Store the path that was attempted/used
        } else if (blockDef.getTexturePath().empty()) { // Only log if specific block had no path and no default
            std::cout << "ResourceManager: Block ID " << blockID << " has no texture path, using default: " << defaultTexturePath_ << std::endl;
            // If defaultTexturePath_ is also empty, texturePathKey will remain empty.
            // If defaultTexturePath_ is set, texturePathToLoad would have been it.
        }
        // else resolved.textureLoader remains nullptr, will be handled by getTextureForBlockType

        m_resolvedBlockAssets[blockID] = resolved;

        std::cout << "ResourceManager: Resolved assets for block ID " << blockDef.getID() // Use blockDef.getID() for consistency
                  << " (Model Path Used: '" << (!modelPathToLoad.empty() ? modelPathToLoad : "None")
                  << "', Texture Path Used: '" << (!texturePathToLoad.empty() ? texturePathToLoad : "None")
                  << "')" << std::endl;
    }
    std::cout << "ResourceManager: Finished loading assets from block registry." << std::endl;
}

void ResourceManager::setDefaultModelPath(const std::string& path) {
    defaultModelPath_ = path;
    std::cout << "ResourceManager: Default model path set to: " << path << std::endl;
}

void ResourceManager::setDefaultTexturePath(const std::string& path) {
    defaultTexturePath_ = path;
    std::cout << "ResourceManager: Default texture path set to: " << path << std::endl;
}

std::shared_ptr<ModelData> ResourceManager::internalLoadModel(const std::string& path, bool isFallbackAttempt) const {
    if (path.empty()) {
        throw std::runtime_error("ResourceManager: Attempted to load model with an empty path.");
    }

    auto it = loadedModels_.find(path);
    if (it != loadedModels_.end()) {
        return it->second;
    }

    std::cout << "ResourceManager: Loading model: " << path << std::endl;
    auto newModelDataPtr = std::make_shared<ModelData>();
    if (!ModelLoader::loadGltfModel(path, *newModelDataPtr)) {
        std::cerr << "ResourceManager Error: Failed to load model: " << path << std::endl;
        if (!isFallbackAttempt && !defaultModelPath_.empty() && path != defaultModelPath_) {
            std::cout << "ResourceManager: Attempting to load default model: " << defaultModelPath_ << std::endl;
            return internalLoadModel(defaultModelPath_, true); // True to prevent infinite recursion on default failing
        }
        // Return nullptr if critical failure and no fallback, or fallback also failed
        return nullptr;
    }
    loadedModels_[path] = newModelDataPtr;
    return newModelDataPtr;
}

std::shared_ptr<VulkanTextureLoader> ResourceManager::internalLoadTexture(const std::string& path, bool isFallbackAttempt) const {
    if (path.empty()) {
        throw std::runtime_error("ResourceManager: Attempted to load texture with an empty path.");
    }

    auto it = loadedTextures_.find(path);
    if (it != loadedTextures_.end()) {
        return it->second;
    }

    std::cout << "ResourceManager: Loading texture: " << path << std::endl;
    try {
        auto newTextureLoader = std::make_shared<VulkanTextureLoader>(physicalDevice_, device_, commandPool_, graphicsQueue_, path);
        loadedTextures_[path] = newTextureLoader;
        return newTextureLoader;
    } catch (const std::runtime_error& e) {
        std::cerr << "ResourceManager Error: Failed to load texture '" << path << "': " << e.what() << std::endl;
        if (!isFallbackAttempt && !defaultTexturePath_.empty() && path != defaultTexturePath_) {
            std::cout << "ResourceManager: Attempting to load default texture: " << defaultTexturePath_ << std::endl;
            return internalLoadTexture(defaultTexturePath_, true); // True to prevent infinite recursion
        }
        // Return nullptr if critical failure and no fallback, or fallback also failed
        return nullptr;
    }
}

std::shared_ptr<const ModelData> ResourceManager::getModelForBlockType(uint16_t blockID) const {
    auto it = m_resolvedBlockAssets.find(blockID);
    if (it != m_resolvedBlockAssets.end() && it->second.modelData) {
        return it->second.modelData;
    }

    // If not found in resolved assets or modelData is null, try default
    std::cerr << "ResourceManager Warning: No resolved model for block ID " << blockID 
              << ". Attempting to use default model: " << defaultModelPath_ << std::endl;
    if (!defaultModelPath_.empty()) {
        auto defaultModel = internalLoadModel(defaultModelPath_, true);
        if (defaultModel) {
            return defaultModel;
        }
        // If defaultModel is nullptr here, internalLoadModel failed to load the default
    }
    // If we reach here, either no default path was set, or loading the default model failed.
    throw std::runtime_error("ResourceManager Critical Error: Could not load model for block ID " + std::to_string(blockID) +
                             " and default model ('" + defaultModelPath_ + "') is either not set or failed to load.");
}

std::shared_ptr<VulkanTextureLoader> ResourceManager::getTextureForBlockType(uint16_t blockID) const {
    auto it = m_resolvedBlockAssets.find(blockID);
    if (it != m_resolvedBlockAssets.end() && it->second.textureLoader) {
        return it->second.textureLoader;
    }

    // If not found in resolved assets or textureLoader is null, try default
    std::cerr << "ResourceManager Warning: No resolved texture for block ID " << blockID 
              << ". Attempting to use default texture: " << defaultTexturePath_ << std::endl;
    if (!defaultTexturePath_.empty()) {
        auto defaultTexture = internalLoadTexture(defaultTexturePath_, true);
        if (defaultTexture) {
            return defaultTexture;
        }
        // If defaultTexture is nullptr here, internalLoadTexture failed to load the default
    }
    // If we reach here, either no default path was set, or loading the default texture failed.
    throw std::runtime_error("ResourceManager Critical Error: Could not load texture for block ID " + std::to_string(blockID) +
                             " and default texture ('" + defaultTexturePath_ + "') is either not set or failed to load.");
}

namespace { // Anonymous namespace for helper functions

void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, 
                           VkImageLayout oldLayout, VkImageLayout newLayout, 
                           uint32_t mipLevels = 1, uint32_t layerCount = 1) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

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
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // Or wherever it was last used
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
     else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        throw std::invalid_argument("Unsupported layout transition in texture atlas generation!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

void ResourceManager::buildTextureAtlas(const BlockRegistry& registry) {
    std::cout << "ResourceManager: Building texture atlas..." << std::endl;
    m_texturePathToAtlasInfoMap.clear();

    std::set<std::string> uniqueTexturePaths;
    if (!defaultTexturePath_.empty()) {
        // Ensure default texture is loaded and add its path
        auto defaultTexLoader = internalLoadTexture(defaultTexturePath_);
        if (defaultTexLoader) {
            uniqueTexturePaths.insert(defaultTexturePath_);
        } else {
            std::cerr << "ResourceManager Critical Error: Default texture '" << defaultTexturePath_ << "' failed to load. Atlas may be incomplete or invalid." << std::endl;
            // Potentially throw here, or try to continue without a default.
        }
    }

    const auto& allDefinitions = registry.getAllBlockDefinitions();
    for (const auto& pair : allDefinitions) {
        const Block& blockDef = pair.second;
        std::string texturePathToUse = !blockDef.getTexturePath().empty() ? blockDef.getTexturePath() : defaultTexturePath_;
        if (!texturePathToUse.empty()) {
            // internalLoadTexture ensures it's in loadedTextures_ if successful
            auto texLoader = internalLoadTexture(texturePathToUse); 
            if (texLoader) {
                uniqueTexturePaths.insert(texturePathToUse); // Use the path that was successfully loaded/cached
            } else {
                std::cerr << "ResourceManager Warning: Texture '" << texturePathToUse << "' for block ID " << blockDef.getID() 
                          << " could not be loaded for atlas. It might use the default texture if available." << std::endl;
            }
        }
    }

    if (uniqueTexturePaths.empty()) {
        std::cout << "ResourceManager: No textures to build atlas from. Atlas will not be created." << std::endl;
        m_defaultAtlasTextureInfo = {{0.0f, 0.0f}, {1.0f, 1.0f}}; // Full UVs for a non-existent atlas
        m_textureAtlas = nullptr; // Ensure atlas is null
        return;
    }

    // Determine the largest texture dimension among the unique textures
    uint32_t largestTextureSize = 0;
    for (const std::string& texturePath : uniqueTexturePaths) {
        auto it = loadedTextures_.find(texturePath);
        if (it != loadedTextures_.end() && it->second) {
            largestTextureSize = std::max({largestTextureSize, it->second->getWidth(), it->second->getHeight()});
        }
    }

    if (largestTextureSize == 0) {
         std::cerr << "ResourceManager Critical Error: Could not determine largest texture size. No valid textures loaded for atlas." << std::endl;
         m_defaultAtlasTextureInfo = {{0.0f, 0.0f}, {1.0f, 1.0f}}; // Full UVs for a non-existent atlas
         m_textureAtlas = nullptr; // Ensure atlas is null
         return;
    }

    uint32_t numUniqueTextures = static_cast<uint32_t>(uniqueTexturePaths.size());
    uint32_t atlasDimInTiles = static_cast<uint32_t>(ceil(sqrt(static_cast<float>(numUniqueTextures))));
    uint32_t atlasPixelWidth = atlasDimInTiles * largestTextureSize;
    uint32_t atlasPixelHeight = atlasDimInTiles * largestTextureSize;
    uint32_t tileTextureSize = largestTextureSize; // Use the determined size as the tile size

    std::cout << "ResourceManager: Atlas: " << numUniqueTextures << " unique textures. Dimensions: " << atlasPixelWidth << "x" << atlasPixelHeight
              << " (" << atlasDimInTiles << "x" << atlasDimInTiles << " tiles of " 
              << tileTextureSize << "x" << tileTextureSize << ")" << std::endl;

    // Use the new VulkanTextureLoader constructor for an empty texture
    // And that it creates a sampler if the last bool is true.
    // And that its image is initially in VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
    m_textureAtlas = std::make_unique<VulkanTextureLoader>(
        physicalDevice_, device_, commandPool_, graphicsQueue_,
        atlasPixelWidth, atlasPixelHeight, VK_FORMAT_R8G8B8A8_SRGB, /* Assumed format */
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_TILING_OPTIMAL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        true // Create a sampler for the atlas
    );

    VkCommandBuffer commandBuffer = VulkanTextureLoader::beginSingleTimeCommands(device_, commandPool_); // Use static method

    // Transition atlas image to TRANSFER_DST_OPTIMAL
    // m_textureAtlas->getImage() now exists.
    // Initial layout is VK_IMAGE_LAYOUT_UNDEFINED from the new VTL constructor.
    transitionImageLayout(commandBuffer, m_textureAtlas->getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    uint32_t currentTileX = 0;
    uint32_t currentTileY = 0;
    for (const std::string& texturePath : uniqueTexturePaths) {
        auto it = loadedTextures_.find(texturePath);
        if (it == loadedTextures_.end() || !it->second) { // Should not happen if logic above is correct
            std::cerr << "ResourceManager Error: Texture '" << texturePath << "' missing from cache during atlas copy." << std::endl;
            continue;
        }
        std::shared_ptr<VulkanTextureLoader> sourceTextureLoader = it->second;

        transitionImageLayout(commandBuffer, sourceTextureLoader->getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Use vkCmdBlitImage for potential scaling
        VkImageBlit blitRegion{};
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(sourceTextureLoader->getWidth()), static_cast<int32_t>(sourceTextureLoader->getHeight()), 1};
        
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstOffsets[0] = {static_cast<int32_t>(currentTileX * largestTextureSize), static_cast<int32_t>(currentTileY * largestTextureSize), 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>((currentTileX + 1) * largestTextureSize), static_cast<int32_t>((currentTileY + 1) * largestTextureSize), 1};

        // Use linear filter for scaling
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_LINEAR);

        transitionImageLayout(commandBuffer, sourceTextureLoader->getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        AtlasTextureInfo info;
        info.uvOffset = {static_cast<float>(currentTileX * tileTextureSize) / atlasPixelWidth, static_cast<float>(currentTileY * tileTextureSize) / atlasPixelHeight};
        info.uvScale = {static_cast<float>(tileTextureSize) / atlasPixelWidth, static_cast<float>(tileTextureSize) / atlasPixelHeight};
        m_texturePathToAtlasInfoMap[texturePath] = info;

        if (texturePath == defaultTexturePath_) {
            m_defaultAtlasTextureInfo = info;
        }
        
        currentTileX++;
        if (currentTileX >= atlasDimInTiles) {
            currentTileX = 0;
            currentTileY++;
        }
    }

    transitionImageLayout(commandBuffer, m_textureAtlas->getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VulkanTextureLoader::endSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer); // Use static method

    if (defaultTexturePath_.empty() || m_texturePathToAtlasInfoMap.find(defaultTexturePath_) == m_texturePathToAtlasInfoMap.end()) {
        if (!m_texturePathToAtlasInfoMap.empty()) {
            m_defaultAtlasTextureInfo = m_texturePathToAtlasInfoMap.begin()->second; // Fallback to first texture
            std::cout << "ResourceManager: Default texture UVs set to first texture in atlas." << std::endl;
        } else { // Atlas is effectively empty or only had problematic textures
            m_defaultAtlasTextureInfo = {{0.0f, 0.0f}, {1.0f, 1.0f}};
            std::cout << "ResourceManager Warning: Atlas is empty or default texture is unavailable. Default UVs cover full area." << std::endl;
        }
    }
    std::cout << "ResourceManager: Texture atlas built successfully." << std::endl;
}

VkImageView ResourceManager::getAtlasImageView() const {
    if (!m_textureAtlas) {
        // Consider throwing an error or returning a handle to a dummy/default texture view
        std::cerr << "ResourceManager Error: Atlas image view requested but atlas is not built or is invalid." << std::endl;
        return VK_NULL_HANDLE; 
    }
    return m_textureAtlas->getImageView(); // Assumes VulkanTextureLoader::getImageView() exists
}

VkSampler ResourceManager::getAtlasSampler() const {
    if (!m_textureAtlas) {
        std::cerr << "ResourceManager Error: Atlas sampler requested but atlas is not built or is invalid." << std::endl;
        return VK_NULL_HANDLE;
    }
    return m_textureAtlas->getSampler(); // Assumes VulkanTextureLoader::getSampler() exists
}

AtlasTextureInfo ResourceManager::getBlockAtlasInfo(uint16_t blockID) const {
    auto resolvedIt = m_resolvedBlockAssets.find(blockID);
    if (resolvedIt != m_resolvedBlockAssets.end()) {
        const ResolvedBlockAssets& assets = resolvedIt->second; // This is a copy, not a reference. Corrected.
        // Use the stored texturePathKey from ResolvedBlockAssets
        if (!assets.texturePathKey.empty()) {
            auto atlasInfoIt = m_texturePathToAtlasInfoMap.find(assets.texturePathKey);
            if (atlasInfoIt != m_texturePathToAtlasInfoMap.end()) {
                return atlasInfoIt->second;
            }
        }
    }
    // Fallback to default texture info if block-specific not found or no specific texture
    return m_defaultAtlasTextureInfo;
}

AtlasTextureInfo ResourceManager::getTextureAtlasInfo(const std::string& texturePath) const {
    auto it = m_texturePathToAtlasInfoMap.find(texturePath);
    if (it != m_texturePathToAtlasInfoMap.end()) {
        return it->second;
    }
    // If specific path not in atlas (e.g. it failed to load), return default.
    std::cerr << "ResourceManager Warning: Texture path '" << texturePath << "' not found in atlas map. Returning default atlas info." << std::endl;
    return m_defaultAtlasTextureInfo;
}