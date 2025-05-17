#include "ResourceManager.hpp"
#include <iostream> // For error messages/logging
#include <stdexcept> // For std::runtime_error
#include <set>      // For std::set to collect unique texture paths
#include <cmath>    // For ceil/sqrt in atlas dimension calculation
#include <algorithm> // For std::max
#include <limits>    // For std::numeric_limits in model analysis
// Block.hpp and BlockRegistry.hpp are included via ResourceManager.hpp
// #include "../Block.hpp"
// #include "../BlockRegistry.hpp"

ResourceManager::ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue)
    : physicalDevice_(physicalDevice), device_(device), commandPool_(commandPool), 
      graphicsQueue_(graphicsQueue), // Initialize the new member
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
    if (m_customAtlasSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, m_customAtlasSampler, nullptr);
    }
    // loadedModels_ will clean up ModelData instances (which are just CPU data)
    std::cout << "ResourceManager destroyed." << std::endl;
}

void ResourceManager::loadAssetsFromRegistry(BlockRegistry& registry, bool preLoadAll) { // Takes non-const registry
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
        std::shared_ptr<ModelData> rawModelData = nullptr; // Store raw model data temporarily
        if (!modelPathToLoad.empty()) {
            rawModelData = internalLoadModel(modelPathToLoad); // Load raw model
            // If model loaded successfully, analyze it and set properties on the block definition
            if (rawModelData) {
                Block* modifiableBlockDef = registry.getBlockDefinitionForModification(blockID);
                if (modifiableBlockDef) {
                    // Analyze properties and get the separated model data
                    resolved.separableModelData = this->analyzeModelAndSetProperties(*modifiableBlockDef, *rawModelData);
                } else {
                    std::cerr << "ResourceManager Error: Could not get modifiable block definition for ID " << blockID << " for model analysis and separation." << std::endl;
                }
            }
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

    // Attempt to find with a shared lock first
    {
        std::shared_lock<std::shared_mutex> lock(m_cache_mutex);
        auto it = loadedModels_.find(path);
        if (it != loadedModels_.end()) {
            return it->second;
        }
    }

    std::cout << "ResourceManager: Loading model: " << path << std::endl;
    auto newModelDataPtr = std::make_shared<ModelData>();
    // Actual file loading is done outside the unique lock to avoid holding it for long
    if (!ModelLoader::loadGltfModel(path, *newModelDataPtr)) {
        std::cerr << "ResourceManager Error: Failed to load model: " << path << std::endl;
        if (!isFallbackAttempt && !defaultModelPath_.empty() && path != defaultModelPath_) {
            std::cout << "ResourceManager: Attempting to load default model: " << defaultModelPath_ << std::endl;
            return internalLoadModel(defaultModelPath_, true); // True to prevent infinite recursion on default failing
        }
        // Return nullptr if critical failure and no fallback, or fallback also failed
        return nullptr;
    }

    // Acquire unique lock to insert into the cache
    {
        std::unique_lock<std::shared_mutex> lock(m_cache_mutex);
        // Re-check in case another thread loaded and inserted it while we were loading
        auto it = loadedModels_.find(path);
        if (it != loadedModels_.end()) {
            return it->second; // Return already cached item
        }
        loadedModels_[path] = newModelDataPtr;
        return newModelDataPtr;
    }
}

std::shared_ptr<VulkanTextureLoader> ResourceManager::internalLoadTexture(const std::string& path, bool isFallbackAttempt) const {
    if (path.empty()) {
        throw std::runtime_error("ResourceManager: Attempted to load texture with an empty path.");
    }

    // Attempt to find with a shared lock first
    {
        std::shared_lock<std::shared_mutex> lock(m_cache_mutex);
        auto it = loadedTextures_.find(path);
        if (it != loadedTextures_.end()) {
            return it->second;
        }
    }

    std::cout << "ResourceManager: Loading texture: " << path << std::endl;
    std::shared_ptr<VulkanTextureLoader> newTextureLoader;
    try {
        // Actual texture loading (file I/O, Vulkan calls) done outside unique lock
        newTextureLoader = std::make_shared<VulkanTextureLoader>(physicalDevice_, device_, commandPool_, graphicsQueue_, path);
    } catch (const std::runtime_error& e) {
        std::cerr << "ResourceManager Error: Failed to load texture '" << path << "': " << e.what() << std::endl;
        if (!isFallbackAttempt && !defaultTexturePath_.empty() && path != defaultTexturePath_) {
            std::cout << "ResourceManager: Attempting to load default texture: " << defaultTexturePath_ << std::endl;
            return internalLoadTexture(defaultTexturePath_, true); // True to prevent infinite recursion
        }
        // Return nullptr if critical failure and no fallback, or fallback also failed
        return nullptr;
    }

    // Acquire unique lock to insert into the cache
    {
        std::unique_lock<std::shared_mutex> lock(m_cache_mutex);
        // Re-check in case another thread loaded and inserted it
        auto it = loadedTextures_.find(path);
        if (it != loadedTextures_.end()) {
            return it->second; // Return already cached item
        }
        loadedTextures_[path] = newTextureLoader;
        return newTextureLoader;
    }
}

std::shared_ptr<const SeparableModelData> ResourceManager::getModelForBlockType(uint16_t blockID) const {
    auto it = m_resolvedBlockAssets.find(blockID);
    if (it != m_resolvedBlockAssets.end() && it->second.separableModelData) {
        return it->second.separableModelData;
    }

    // If not found in resolved assets or modelData is null, try default
    std::cerr << "ResourceManager Warning: No resolved model for block ID " << blockID 
              << ". Attempting to use default model: " << defaultModelPath_ << std::endl;
    /*if (!defaultModelPath_.empty()) { // Default model would also need to be separable, this logic needs adjustment
        auto defaultModel = internalLoadModel(defaultModelPath_, true);
        if (defaultModel) {
            return defaultModel;
        }
        // If defaultModel is nullptr here, internalLoadModel failed to load the default
    }
    // If we reach here, either no default path was set, or loading the default model failed.
    */ // TODO: Handle default separable model loading if needed. For now, throw if specific not found.
    throw std::runtime_error("ResourceManager Critical Error: Could not load separable model for block ID " + std::to_string(blockID) +
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

std::shared_ptr<SeparableModelData> ResourceManager::analyzeModelAndSetProperties(Block& blockDef, const ModelData& rawModelData) const {
    auto separableData = std::make_shared<SeparableModelData>();

    std::cout << "ResourceManager: Analyzing model for Block ID: " << blockDef.getID() << " (Model Path: " << blockDef.getModelPath() << ")" << std::endl;

    if (rawModelData.vertices.empty()) {
        // Defaults are already set in Block constructor (no full faces, FULL_MODEL_IF_ANY_EXPOSED strategy)
        // Return an empty SeparableModelData
        std::cout << "  Block ID " << blockDef.getID() << ": Raw model data is empty. Returning empty SeparableModelData." << std::endl;
        return separableData;
    }

    // DIAGNOSTIC: Print AABB of rawModelData
    if (!rawModelData.vertices.empty()) {
        glm::vec3 minBound(std::numeric_limits<float>::max());
        glm::vec3 maxBound(std::numeric_limits<float>::lowest());
        for (const auto& v_diag : rawModelData.vertices) { // Renamed v_diag to avoid conflict
            minBound.x = std::min(minBound.x, v_diag.pos.x);
            minBound.y = std::min(minBound.y, v_diag.pos.y);
            minBound.z = std::min(minBound.z, v_diag.pos.z);
            maxBound.x = std::max(maxBound.x, v_diag.pos.x);
            maxBound.y = std::max(maxBound.y, v_diag.pos.y);
            maxBound.z = std::max(maxBound.z, v_diag.pos.z);
        }
        std::cout << "  Block ID " << blockDef.getID() << ": Raw Model AABB Min: (" << minBound.x << "," << minBound.y << "," << minBound.z 
                  << "), Max: (" << maxBound.x << "," << maxBound.y << "," << maxBound.z << ")" << std::endl;
    }
    // END DIAGNOSTIC

    // --- 1. Analyze and set full face properties on blockDef (existing logic) ---
    std::array<bool, 6> detectedFullFaces{};

    for (int i = 0; i < 6; ++i) {
        FaceDirection currentFaceDir = static_cast<FaceDirection>(i);
        
        float minU_coord = std::numeric_limits<float>::max();
        float maxU_coord = std::numeric_limits<float>::lowest();
        float minV_coord = std::numeric_limits<float>::max();
        float maxV_coord = std::numeric_limits<float>::lowest();
        bool foundVerticesOnPlane = false;

        for (const auto& vertex : rawModelData.vertices) {
            float primaryCoord;
            float uCoord, vCoord;

            if (i == 0 || i == 1) { // +/- X faces (normal along X)
                primaryCoord = vertex.pos.x; uCoord = vertex.pos.y; vCoord = vertex.pos.z;
            } else if (i == 2 || i == 3) { // +/- Y faces (normal along Y)
                primaryCoord = vertex.pos.y; uCoord = vertex.pos.x; vCoord = vertex.pos.z;
            } else { // +/- Z faces (normal along Z)
                primaryCoord = vertex.pos.z; uCoord = vertex.pos.x; vCoord = vertex.pos.y;
            }

            // Adjust for 0-1 range: POS faces are at MAX_EXTENT, NEG faces are at MIN_EXTENT
            float expectedPrimaryCoord = (i % 2 == 0) ? MODEL_ANALYSIS_MAX_EXTENT : MODEL_ANALYSIS_MIN_EXTENT;
            if (std::abs(primaryCoord - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON) {
                foundVerticesOnPlane = true;
                minU_coord = std::min(minU_coord, uCoord);
                maxU_coord = std::max(maxU_coord, uCoord);
                minV_coord = std::min(minV_coord, vCoord);
                maxV_coord = std::max(maxV_coord, vCoord);
            }
        }

        if (foundVerticesOnPlane &&
            std::abs(minU_coord - MODEL_ANALYSIS_MIN_EXTENT) < MODEL_ANALYSIS_EPSILON && std::abs(maxU_coord - MODEL_ANALYSIS_MAX_EXTENT) < MODEL_ANALYSIS_EPSILON &&
            std::abs(minV_coord - MODEL_ANALYSIS_MIN_EXTENT) < MODEL_ANALYSIS_EPSILON && std::abs(maxV_coord - MODEL_ANALYSIS_MAX_EXTENT) < MODEL_ANALYSIS_EPSILON) {
            detectedFullFaces[i] = true;
        }
        std::cout << "  Block ID " << blockDef.getID() << ": FaceDirection " << i << " - Detected as full by geometry: " << detectedFullFaces[i] << std::endl;
        blockDef.setFullOccludingFace(currentFaceDir, detectedFullFaces[i]);
    }

    std::cout << "  Block ID " << blockDef.getID() << ": Starting geometry splitting..." << std::endl;
    // --- 2. Split geometry into SeparableModelData ---
    std::vector<bool> rawTriangleProcessed(rawModelData.indices.size() / 3, false);

    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        FaceDirection currentDir = static_cast<FaceDirection>(faceIdx);
        ModelData& currentFaceModelData = separableData->canonicalFaces[faceIdx];

        if (!blockDef.hasFullOccludingFace(currentDir)) {
            // std::cout << "    Face " << faceIdx << ": BlockDef says not a full occluding face. Skipping extraction." << std::endl;
            continue; // This canonical face isn't "full", so don't try to extract specific geometry for it.
        }

        // Adjust for 0-1 range
        float expectedPrimaryCoord = (faceIdx % 2 == 0) ? MODEL_ANALYSIS_MAX_EXTENT : MODEL_ANALYSIS_MIN_EXTENT;

        for (size_t i = 0; i < rawModelData.indices.size(); i += 3) {
            size_t triIndex = i / 3;
            if (rawTriangleProcessed[triIndex]) continue;

            const Vertex& v0 = rawModelData.vertices[rawModelData.indices[i]];
            const Vertex& v1 = rawModelData.vertices[rawModelData.indices[i+1]];
            const Vertex& v2 = rawModelData.vertices[rawModelData.indices[i+2]];

            // Check if all vertices of the triangle are on the current canonical plane
            bool onPlane = false;
            if (faceIdx == 0 || faceIdx == 1) { // +/- X
                onPlane = std::abs(v0.pos.x - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON &&
                          std::abs(v1.pos.x - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON &&
                          std::abs(v2.pos.x - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON;
            } else if (faceIdx == 2 || faceIdx == 3) { // +/- Y
                onPlane = std::abs(v0.pos.y - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON &&
                          std::abs(v1.pos.y - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON &&
                          std::abs(v2.pos.y - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON;
            } else { // +/- Z
                onPlane = std::abs(v0.pos.z - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON &&
                          std::abs(v1.pos.z - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON &&
                          std::abs(v2.pos.z - expectedPrimaryCoord) < MODEL_ANALYSIS_EPSILON;
            }

            if (onPlane) {
                // Basic check: if on plane and this face is considered "full" by blockDef, add it.
                // More advanced: check triangle normal alignment. For now, this is simpler.
                uint32_t baseIdx = static_cast<uint32_t>(currentFaceModelData.vertices.size());
                currentFaceModelData.vertices.push_back(v0);
                currentFaceModelData.vertices.push_back(v1);
                currentFaceModelData.vertices.push_back(v2);
                currentFaceModelData.indices.push_back(baseIdx);
                currentFaceModelData.indices.push_back(baseIdx + 1);
                currentFaceModelData.indices.push_back(baseIdx + 2);
                rawTriangleProcessed[triIndex] = true;
            }
        }
        std::cout << "    Face " << faceIdx << " (BlockDef full: " << blockDef.hasFullOccludingFace(currentDir) << "): Extracted "
                  << currentFaceModelData.vertices.size() << " verts, " << currentFaceModelData.indices.size() << " indices." << std::endl;
    }

    // Add all unprocessed triangles to remainingGeometry
    for (size_t i = 0; i < rawModelData.indices.size(); i += 3) {
        size_t triIndex = i / 3;
        if (!rawTriangleProcessed[triIndex]) {
            uint32_t baseIdx = static_cast<uint32_t>(separableData->remainingGeometry.vertices.size());
            separableData->remainingGeometry.vertices.push_back(rawModelData.vertices[rawModelData.indices[i]]);
            separableData->remainingGeometry.vertices.push_back(rawModelData.vertices[rawModelData.indices[i+1]]);
            separableData->remainingGeometry.vertices.push_back(rawModelData.vertices[rawModelData.indices[i+2]]);
            separableData->remainingGeometry.indices.push_back(baseIdx);
            separableData->remainingGeometry.indices.push_back(baseIdx + 1);
            separableData->remainingGeometry.indices.push_back(baseIdx + 2);
        }
    }
    std::cout << "  Block ID " << blockDef.getID() << ": Remaining geometry: "
              << separableData->remainingGeometry.vertices.size() << " verts, " << separableData->remainingGeometry.indices.size() << " indices." << std::endl;
    return separableData;
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
    // PADDING constant is now ResourceManager::ATLAS_PADDING

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
    int32_t contentTileSize = 0; // Changed to int32_t
    for (const std::string& texturePath : uniqueTexturePaths) {
        auto it = loadedTextures_.find(texturePath);
        if (it != loadedTextures_.end() && it->second) {
            contentTileSize = std::max({contentTileSize, static_cast<int32_t>(it->second->getWidth()), static_cast<int32_t>(it->second->getHeight())});
        }
    }

    if (contentTileSize == 0) {
         std::cerr << "ResourceManager Critical Error: Could not determine largest texture size. No valid textures loaded for atlas." << std::endl;
         m_defaultAtlasTextureInfo = {{0.0f, 0.0f}, {1.0f, 1.0f}}; // Full UVs for a non-existent atlas
         m_textureAtlas = nullptr; // Ensure atlas is null
         return;
    }

    int32_t paddedTileSize = contentTileSize + 2 * ATLAS_PADDING; 

    uint32_t numUniqueTextures = static_cast<uint32_t>(uniqueTexturePaths.size());
    uint32_t atlasDimInTiles = static_cast<uint32_t>(ceil(sqrt(static_cast<float>(numUniqueTextures))));
    uint32_t atlasPixelWidth = atlasDimInTiles * paddedTileSize;
    uint32_t atlasPixelHeight = atlasDimInTiles * paddedTileSize;
    // uint32_t tileTextureSize = contentTileSize; // Renamed for clarity

    std::cout << "ResourceManager: Atlas: " << numUniqueTextures << " unique textures. Dimensions: " << atlasPixelWidth << "x" << atlasPixelHeight
              << " (" << atlasDimInTiles << "x" << atlasDimInTiles << " tiles of " 
              << paddedTileSize << "x" << paddedTileSize << " with " << contentTileSize << "x" << contentTileSize << " content)" << std::endl;

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

    int32_t currentTileX = 0; // Changed to int32_t
    int32_t currentTileY = 0; // Changed to int32_t
    for (const std::string& texturePath : uniqueTexturePaths) {
        auto it = loadedTextures_.find(texturePath);
        if (it == loadedTextures_.end() || !it->second) { // Should not happen if logic above is correct
            std::cerr << "ResourceManager Error: Texture '" << texturePath << "' missing from cache during atlas copy." << std::endl;
            continue;
        }
        std::shared_ptr<VulkanTextureLoader> sourceTextureLoader = it->second;

        transitionImageLayout(commandBuffer, sourceTextureLoader->getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // --- Blit main content and padding ---
        int32_t srcW = static_cast<int32_t>(sourceTextureLoader->getWidth());
        int32_t srcH = static_cast<int32_t>(sourceTextureLoader->getHeight());

        int32_t dstTileOriginX = currentTileX * paddedTileSize; // Now int32_t * int32_t
        int32_t dstTileOriginY = currentTileY * paddedTileSize; // Now int32_t * int32_t

        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

        // 1. Main content
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {srcW, srcH, 1};
        blit.dstOffsets[0] = {dstTileOriginX + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING + contentTileSize, dstTileOriginY + ATLAS_PADDING + contentTileSize, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 2. Top border (stretch top row of source)
        blit.srcOffsets[0] = {0, 0, 0};            blit.srcOffsets[1] = {srcW, 1, 1};
        blit.dstOffsets[0] = {dstTileOriginX + ATLAS_PADDING, dstTileOriginY, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING + contentTileSize, dstTileOriginY + ATLAS_PADDING, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 3. Bottom border (stretch bottom row of source)
        blit.srcOffsets[0] = {0, srcH - 1, 0};    blit.srcOffsets[1] = {srcW, srcH, 1};
        blit.dstOffsets[0] = {dstTileOriginX + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING + contentTileSize, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING + contentTileSize, dstTileOriginY + ATLAS_PADDING + contentTileSize + ATLAS_PADDING, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 4. Left border (stretch left column of source)
        blit.srcOffsets[0] = {0, 0, 0};            blit.srcOffsets[1] = {1, srcH, 1};
        blit.dstOffsets[0] = {dstTileOriginX, dstTileOriginY + ATLAS_PADDING, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING + contentTileSize, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 5. Right border (stretch right column of source)
        blit.srcOffsets[0] = {srcW - 1, 0, 0};    blit.srcOffsets[1] = {srcW, srcH, 1};
        blit.dstOffsets[0] = {dstTileOriginX + ATLAS_PADDING + contentTileSize, dstTileOriginY + ATLAS_PADDING, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING + contentTileSize + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING + contentTileSize, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 6. Top-Left Corner (stretch top-left pixel of source)
        blit.srcOffsets[0] = {0, 0, 0};            blit.srcOffsets[1] = {1, 1, 1};
        blit.dstOffsets[0] = {dstTileOriginX, dstTileOriginY, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 7. Top-Right Corner
        blit.srcOffsets[0] = {srcW - 1, 0, 0};    blit.srcOffsets[1] = {srcW, 1, 1};
        blit.dstOffsets[0] = {dstTileOriginX + ATLAS_PADDING + contentTileSize, dstTileOriginY, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING + contentTileSize + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 8. Bottom-Left Corner
        blit.srcOffsets[0] = {0, srcH - 1, 0};    blit.srcOffsets[1] = {1, srcH, 1};
        blit.dstOffsets[0] = {dstTileOriginX, dstTileOriginY + ATLAS_PADDING + contentTileSize, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING + contentTileSize + ATLAS_PADDING, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        // 9. Bottom-Right Corner
        blit.srcOffsets[0] = {srcW - 1, srcH - 1, 0}; blit.srcOffsets[1] = {srcW, srcH, 1};
        blit.dstOffsets[0] = {dstTileOriginX + ATLAS_PADDING + contentTileSize, dstTileOriginY + ATLAS_PADDING + contentTileSize, 0};
        blit.dstOffsets[1] = {dstTileOriginX + ATLAS_PADDING + contentTileSize + ATLAS_PADDING, dstTileOriginY + ATLAS_PADDING + contentTileSize + ATLAS_PADDING, 1};
        vkCmdBlitImage(commandBuffer, sourceTextureLoader->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        transitionImageLayout(commandBuffer, sourceTextureLoader->getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        AtlasTextureInfo info;
        // UVs now point to the content area within the padded tile

        // Calculate a small inset in UV space (e.g., half an atlas pixel)
        // This helps prevent sampling the very edge pixels that might be affected by mipmap filtering
        // when viewing at extreme angles or distances.
        float uvPixelXDim = 1.0f / static_cast<float>(atlasPixelWidth);
        float uvPixelYDim = 1.0f / static_cast<float>(atlasPixelHeight);
        // Calculate the number of atlas pixels for the inset based on a percentage of contentTileSize
        float inset_atlas_pixels = static_cast<float>(contentTileSize) * ATLAS_UV_INSET_FACTOR_OF_CONTENT;
        float uInset = uvPixelXDim * inset_atlas_pixels;
        float vInset = uvPixelYDim * inset_atlas_pixels;

        info.uvOffset = {
            (static_cast<float>(dstTileOriginX + ATLAS_PADDING) / atlasPixelWidth) + uInset,
            (static_cast<float>(dstTileOriginY + ATLAS_PADDING) / atlasPixelHeight) + vInset
        };
        info.uvScale = {
            (static_cast<float>(contentTileSize) / atlasPixelWidth) - (2.0f * uInset),
            (static_cast<float>(contentTileSize) / atlasPixelHeight) - (2.0f * vInset)
        };
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

    // Create a custom sampler for the atlas with LOD clamping
    if (m_textureAtlas && m_textureAtlas->getImage() != VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST; // Or VK_FILTER_NEAREST for pixelated look
        samplerInfo.minFilter = VK_FILTER_LINEAR; // Linear for smoother mip transitions
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // Or CLAMP_TO_EDGE, etc.
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE; // Enable anisotropy
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy; // Use max supported
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // Linear for smoother mip transitions
        samplerInfo.minLod = 0.0f;
        
        uint32_t totalMipLevels = m_textureAtlas->getMipLevels(); // Assuming VulkanTextureLoader has this
        float calculatedMaxLod = static_cast<float>(totalMipLevels - 1); // Default max LOD is the highest mip index

        if (totalMipLevels > ATLAS_CLAMP_LOWEST_MIP_LEVELS) {
            calculatedMaxLod = static_cast<float>(totalMipLevels - 1 - ATLAS_CLAMP_LOWEST_MIP_LEVELS);
        }
        samplerInfo.maxLod = std::max(0.0f, calculatedMaxLod); // Ensure maxLod is not less than minLod (0.0)
        // std::cout << "Atlas Sampler: Total Mips: " << totalMipLevels << ", Clamping: " << ATLAS_CLAMP_LOWEST_MIP_LEVELS << ", MaxLOD set to: " << samplerInfo.maxLod << std::endl;

        if (vkCreateSampler(device_, &samplerInfo, nullptr, &m_customAtlasSampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create custom texture atlas sampler!");
        }
    }

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
        std::cerr << "ResourceManager Error: Custom atlas sampler requested but atlas is not built or is invalid." << std::endl;
        return VK_NULL_HANDLE;
    }
    return m_customAtlasSampler; // Return our custom sampler
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