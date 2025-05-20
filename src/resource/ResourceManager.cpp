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
            // Also, get the separable model data (which will be a copy for this block)
            if (rawModelData) {
                // Get (cached) analysis results
                CachedModelAnalysis analysis = getOrPerformAnalysis(modelPathToLoad, *rawModelData);

                // Apply face properties to the block definition
                Block* modifiableBlockDef = registry.getBlockDefinitionForModification(blockID);
                if (modifiableBlockDef) {
                    std::cout << "ResourceManager: Applying analysis results to Block ID: " << blockID 
                              << " (Model Path: " << modelPathToLoad << ")" << std::endl;
                    for (int face_idx = 0; face_idx < 6; ++face_idx) {
                        modifiableBlockDef->setFullOccludingFace(static_cast<FaceDirection>(face_idx), analysis.faceProperties[face_idx]);
                    }
                } else {
                    std::cerr << "ResourceManager Error: Could not get modifiable block definition for ID " << blockID << " for applying face properties." << std::endl;
                }
                // Create a unique copy of SeparableModelData for this block, to be populated with specific atlas UVs later
                if (analysis.rawSeparableModel) {
                    resolved.separableModelData = std::make_shared<SeparableModelData>(*analysis.rawSeparableModel); // Deep copy
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
    // After processing all blocks and potentially loading assets, build the atlas
    buildTextureAtlas(registry);
    finalizeSeparableModelAtlasInfos(); // New step: Populate atlas infos in separable models
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

CachedModelAnalysis ResourceManager::getOrPerformAnalysis(const std::string& modelPath, const ModelData& rawModelData) const {
    // Check cache first (read lock)
    {
        std::shared_lock<std::shared_mutex> lock(m_analysis_cache_mutex);
        auto it = m_modelAnalysisCache.find(modelPath);
        if (it != m_modelAnalysisCache.end()) {
            std::cout << "ResourceManager: Using cached analysis for model path: " << modelPath << std::endl;
            return it->second;
        }
    }

    // Not in cache, perform analysis (this part is outside the write lock initially for performance)
    std::cout << "ResourceManager: Performing analysis for model path: " << modelPath << std::endl;

    std::array<bool, 6> detectedFullFaces{};
    auto newSeparableData = std::make_shared<SeparableModelData>(); // This will hold the geometry

    if (rawModelData.vertices.empty()) {
        std::cout << "  Model Path " << modelPath << ": Raw model data is empty. Analysis yields no full faces and empty separable data." << std::endl;
        // detectedFullFaces is already all false. newSeparableData is empty.
        CachedModelAnalysis emptyAnalysisResult = {detectedFullFaces, newSeparableData};
        // Store this "empty" analysis result in cache to avoid re-processing empty models
        {
            std::unique_lock<std::shared_mutex> lock(m_analysis_cache_mutex);
            // Double-check, another thread might have finished analysis
            auto it = m_modelAnalysisCache.find(modelPath);
            if (it != m_modelAnalysisCache.end()) {
                return it->second;
            }
            m_modelAnalysisCache[modelPath] = emptyAnalysisResult;
            return emptyAnalysisResult;
        }
    }

    // DIAGNOSTIC: Print AABB of rawModelData
    if (!rawModelData.vertices.empty()) {
        glm::vec3 minBound(std::numeric_limits<float>::max());
        glm::vec3 maxBound(std::numeric_limits<float>::lowest());
        for (const auto& v_diag : rawModelData.vertices) {
            minBound.x = std::min(minBound.x, v_diag.pos.x);
            minBound.y = std::min(minBound.y, v_diag.pos.y);
            minBound.z = std::min(minBound.z, v_diag.pos.z);
            maxBound.x = std::max(maxBound.x, v_diag.pos.x);
            maxBound.y = std::max(maxBound.y, v_diag.pos.y);
            maxBound.z = std::max(maxBound.z, v_diag.pos.z);
        }
        std::cout << "  Model Path " << modelPath << ": Raw Model AABB Min: (" << minBound.x << "," << minBound.y << "," << minBound.z 
                  << "), Max: (" << maxBound.x << "," << maxBound.y << "," << maxBound.z << ")" << std::endl;
    }
    // END DIAGNOSTIC

    // --- 1. Analyze for full face properties ---

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
        std::cout << "  Model Path " << modelPath << ": FaceDirection " << i << " - Detected as full by geometry: " << detectedFullFaces[i] << std::endl;
    }

    std::cout << "  Model Path " << modelPath << ": Starting geometry splitting for separable model template..." << std::endl;
    // --- 2. Split geometry into SeparableModelData ---
    std::vector<bool> rawTriangleProcessed(rawModelData.indices.size() / 3, false);

    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        FaceDirection currentDir = static_cast<FaceDirection>(faceIdx);
        ModelData& currentFaceModelData = newSeparableData->canonicalFaces[faceIdx];

        // We extract geometry for a face if the analysis determined it's a full face.
        // The blockDef's properties will be set later using detectedFullFaces.
        if (!detectedFullFaces[faceIdx]) {
            continue; 
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
        std::cout << "    Face " << faceIdx << " (Geometrically full: " << detectedFullFaces[faceIdx] << "): Extracted "
                  << currentFaceModelData.vertices.size() << " verts, " << currentFaceModelData.indices.size() << " indices." << std::endl;
    }

    // Add all unprocessed triangles to remainingGeometry
    for (size_t i = 0; i < rawModelData.indices.size(); i += 3) {
        size_t triIndex = i / 3;
        if (!rawTriangleProcessed[triIndex]) {
            uint32_t baseIdx = static_cast<uint32_t>(newSeparableData->remainingGeometry.vertices.size());
            newSeparableData->remainingGeometry.vertices.push_back(rawModelData.vertices[rawModelData.indices[i]]);
            newSeparableData->remainingGeometry.vertices.push_back(rawModelData.vertices[rawModelData.indices[i+1]]);
            newSeparableData->remainingGeometry.vertices.push_back(rawModelData.vertices[rawModelData.indices[i+2]]);
            newSeparableData->remainingGeometry.indices.push_back(baseIdx);
            newSeparableData->remainingGeometry.indices.push_back(baseIdx + 1);
            newSeparableData->remainingGeometry.indices.push_back(baseIdx + 2);
        }
    }
    std::cout << "  Model Path " << modelPath << ": Remaining geometry for template: "
              << newSeparableData->remainingGeometry.vertices.size() << " verts, " << newSeparableData->remainingGeometry.indices.size() << " indices." << std::endl;

    CachedModelAnalysis analysisResult = {detectedFullFaces, newSeparableData};

    // Store in cache (write lock)
    {
        std::unique_lock<std::shared_mutex> lock(m_analysis_cache_mutex);
        // Double-check, another thread might have finished analysis and inserted it
        auto it = m_modelAnalysisCache.find(modelPath);
        if (it != m_modelAnalysisCache.end()) {
            return it->second; // Return data from other thread
        }
        m_modelAnalysisCache[modelPath] = analysisResult;
        return analysisResult;
    }
}

void ResourceManager::finalizeSeparableModelAtlasInfos() {
    std::cout << "ResourceManager: Finalizing AtlasTextureInfo for separable models..." << std::endl;
    if (m_texturePathToAtlasInfoMap.empty()) {
        std::cerr << "ResourceManager Warning: Texture atlas map is empty. Cannot finalize AtlasTextureInfo for models." << std::endl;
        return;
    }

    for (auto& pair : m_resolvedBlockAssets) {
        // uint16_t blockID = pair.first; // Not directly needed here
        ResolvedBlockAssets& resolvedAsset = pair.second; // Get a non-const reference

        if (!resolvedAsset.separableModelData) {
            continue; // No model data to process
        }

        // Determine the AtlasTextureInfo to use for this block's model parts
        AtlasTextureInfo specificAtlasInfo = m_defaultAtlasTextureInfo; // Start with default
        if (!resolvedAsset.texturePathKey.empty()) {
            auto atlasInfoIt = m_texturePathToAtlasInfoMap.find(resolvedAsset.texturePathKey);
            if (atlasInfoIt != m_texturePathToAtlasInfoMap.end()) {
                specificAtlasInfo = atlasInfoIt->second;
            } else {
                std::cerr << "ResourceManager Warning: Texture path '" << resolvedAsset.texturePathKey 
                          << "' not found in atlas map during finalization. Using default atlas info for block ID " << pair.first << std::endl;
            }
        }

        // Populate for canonical faces
        for (int i = 0; i < 6; ++i) {
            ModelData& faceModel = resolvedAsset.separableModelData->canonicalFaces[i];
            for (Vertex& vertex : faceModel.vertices) {
                vertex.atlasUvOffset = specificAtlasInfo.uvOffset;
                vertex.atlasUvScale = specificAtlasInfo.uvScale;
            }
        }

        // Populate for remaining geometry
        ModelData& remainingModel = resolvedAsset.separableModelData->remainingGeometry;
        for (Vertex& vertex : remainingModel.vertices) {
            vertex.atlasUvOffset = specificAtlasInfo.uvOffset;
            vertex.atlasUvScale = specificAtlasInfo.uvScale;
        }
    }
    std::cout << "ResourceManager: AtlasTextureInfo finalization complete." << std::endl;
}

namespace { // Anonymous namespace for helper functions

void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, 
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t baseMipLevel, uint32_t numMipLevels, uint32_t baseArrayLayer = 0, uint32_t numArrayLayers = 1) {
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
    
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = numMipLevels;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = numArrayLayers;

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
    // Default texture is still important to ensure it's loaded if used by any block.
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
    // This will be the size of each cell in the atlas at mip level 0.
    // All source textures will be blitted (potentially scaled) into a cell of this size.
    uint32_t maxContentTileSizeMip0 = 0; 
    for (const std::string& texturePath : uniqueTexturePaths) {
        auto it = loadedTextures_.find(texturePath);
        if (it != loadedTextures_.end() && it->second) {
            maxContentTileSizeMip0 = std::max({maxContentTileSizeMip0, it->second->getWidth(), it->second->getHeight()});
        }
    }

    if (maxContentTileSizeMip0 == 0) {
         std::cerr << "ResourceManager Critical Error: Could not determine largest texture size. No valid textures loaded for atlas." << std::endl;
         m_defaultAtlasTextureInfo = {{0.0f, 0.0f}, {1.0f, 1.0f}}; // Full UVs for a non-existent atlas
         m_textureAtlas = nullptr; // Ensure atlas is null
         return;
    }

    // Calculate atlas dimensions based on content size at mip 0 plus padding at mip 0
    uint32_t paddedCellSizeMip0 = maxContentTileSizeMip0 + 2 * ATLAS_PADDING_MIP0;

    uint32_t numUniqueTextures = static_cast<uint32_t>(uniqueTexturePaths.size());
    uint32_t atlasDimInTiles = static_cast<uint32_t>(ceil(sqrt(static_cast<float>(numUniqueTextures))));
    
    // Calculate initial atlas dimensions
    uint32_t initialAtlasPixelWidth = atlasDimInTiles * paddedCellSizeMip0;
    uint32_t initialAtlasPixelHeight = atlasDimInTiles * paddedCellSizeMip0;

    // Determine atlasTotalMipLevels based on initial dimensions
    // This is the number of mips the atlas itself will have.
    uint32_t atlasTotalMipLevelsForSizing = 1;
    if (initialAtlasPixelWidth > 0 && initialAtlasPixelHeight > 0) { // Ensure non-zero dimensions
        atlasTotalMipLevelsForSizing = static_cast<uint32_t>(std::floor(std::log2(std::max(initialAtlasPixelWidth, initialAtlasPixelHeight)))) + 1;
    }

    // Adjust atlas dimensions to be mip-friendly
    uint32_t divisor = (atlasTotalMipLevelsForSizing > 1) ? (1u << (atlasTotalMipLevelsForSizing - 1)) : 1u;
    uint32_t atlasPixelWidth = ((initialAtlasPixelWidth + divisor - 1) / divisor) * divisor; // Round up to nearest multiple of divisor
    uint32_t atlasPixelHeight = ((initialAtlasPixelHeight + divisor - 1) / divisor) * divisor; // Round up

    std::cout << "ResourceManager: Atlas: " << numUniqueTextures << " unique textures. Dimensions: " << atlasPixelWidth << "x" << atlasPixelHeight
              << " (" << atlasDimInTiles << "x" << atlasDimInTiles << " tiles of " 
              << paddedCellSizeMip0 << "x" << paddedCellSizeMip0 << " (padded cell size at mip0). Content area at mip0: " 
              << maxContentTileSizeMip0 << "x" << maxContentTileSizeMip0 << ")" << std::endl;

    m_textureAtlas = std::make_unique<VulkanTextureLoader>(
        physicalDevice_, device_, commandPool_, graphicsQueue_,
        atlasPixelWidth, atlasPixelHeight, VK_FORMAT_R8G8B8A8_SRGB, /* Assumed format */
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // SRC might be useful if we ever blit from atlas, DST is key.
        VK_IMAGE_TILING_OPTIMAL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        true // Create a sampler for the atlas
    );

    VkCommandBuffer commandBuffer = VulkanTextureLoader::beginSingleTimeCommands(device_, commandPool_); // Use static method

    // Determine the maximum number of mip levels any source texture has.
    // The atlas will have mip levels based on its own dimensions.
    uint32_t atlasTotalMipLevels = 1;
    if (m_textureAtlas) { // m_textureAtlas->getMipLevels() is based on atlas dimensions
        atlasTotalMipLevels = m_textureAtlas->getMipLevels();
    }

    for (uint32_t mipLevel = 0; mipLevel < atlasTotalMipLevels; ++mipLevel) {
        // Transition current atlas mip level to TRANSFER_DST_OPTIMAL
        // It's initially UNDEFINED for all mips.
        ::transitionImageLayout(commandBuffer, m_textureAtlas->getImage(), VK_FORMAT_R8G8B8A8_SRGB,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                mipLevel, 1, 0, 1); // baseMip, numMips, baseLayer, numLayers

        uint32_t currentTileX = 0;
        uint32_t currentTileY = 0;

        uint32_t currentMipPadding = std::max(1u, ATLAS_PADDING_MIP0 >> mipLevel);
        uint32_t dstContentSizeThisMip = std::max(1u, maxContentTileSizeMip0 >> mipLevel);
        uint32_t dstPaddedCellSizeThisMip = dstContentSizeThisMip + 2 * currentMipPadding;

        // Get actual atlas mip dimensions for this specific mipLevel
        // These are the true boundaries we cannot exceed.
        const uint32_t actualAtlasMipWidth = std::max(1u, m_textureAtlas->getWidth() >> mipLevel);
        const uint32_t actualAtlasMipHeight = std::max(1u, m_textureAtlas->getHeight() >> mipLevel);

        auto performClampedBlit = 
            [&](VkImageBlit& currentBlit, VkFilter filter, VkImage srcImage) { // Pass srcImage

            // Check if the start of the blit is already out of bounds
            if (currentBlit.dstOffsets[0].x >= static_cast<int32_t>(actualAtlasMipWidth) ||
                currentBlit.dstOffsets[0].y >= static_cast<int32_t>(actualAtlasMipHeight)) {
                return; // Skip blit, it's entirely outside
            }

            // Clamp the end of the blit region to the actual atlas mip dimensions
            currentBlit.dstOffsets[1].x = std::min(currentBlit.dstOffsets[1].x, static_cast<int32_t>(actualAtlasMipWidth));
            currentBlit.dstOffsets[1].y = std::min(currentBlit.dstOffsets[1].y, static_cast<int32_t>(actualAtlasMipHeight));
            
            // Ensure start is not past the (potentially clamped) end.
            // This handles cases where dstOffsets[0] was valid but dstOffsets[1] got clamped to be <= dstOffsets[0].
            currentBlit.dstOffsets[0].x = std::min(currentBlit.dstOffsets[0].x, currentBlit.dstOffsets[1].x);
            currentBlit.dstOffsets[0].y = std::min(currentBlit.dstOffsets[0].y, currentBlit.dstOffsets[1].y);

            // If clamping made width or height non-positive, skip
            if (currentBlit.dstOffsets[1].x > currentBlit.dstOffsets[0].x && 
                currentBlit.dstOffsets[1].y > currentBlit.dstOffsets[0].y) {
                
                vkCmdBlitImage(commandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_textureAtlas->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &currentBlit, filter);
            }
        };

        for (const std::string& texturePath : uniqueTexturePaths) {
            auto it = loadedTextures_.find(texturePath);
            if (it == loadedTextures_.end() || !it->second) {
                std::cerr << "ResourceManager Error: Texture '" << texturePath << "' missing from cache during atlas mip " << mipLevel << " copy." << std::endl;
                currentTileX++; // Still advance tile position
                if (currentTileX >= atlasDimInTiles) {
                    currentTileX = 0;
                    currentTileY++;
                }
                continue;
            }
            std::shared_ptr<VulkanTextureLoader> sourceTextureLoader = it->second;

            if (mipLevel < sourceTextureLoader->getMipLevels()) {
                // Transition source texture's current mip level to TRANSFER_SRC_OPTIMAL
                // Source textures are loaded with all mips in SHADER_READ_ONLY_OPTIMAL
                ::transitionImageLayout(commandBuffer, sourceTextureLoader->getImage(), sourceTextureLoader->getFormat(),
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                           mipLevel, 1, 0, 1);

                uint32_t srcMipWidth = std::max(1u, sourceTextureLoader->getWidth() >> mipLevel);
                uint32_t srcMipHeight = std::max(1u, sourceTextureLoader->getHeight() >> mipLevel);

                uint32_t dstCellOriginX_atlas_mip = currentTileX * dstPaddedCellSizeThisMip;
                uint32_t dstCellOriginY_atlas_mip = currentTileY * dstPaddedCellSizeThisMip;

                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 0, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 0, 1};

                // 1. Blit Main Content (scales source mip to fit content area of atlas cell)
                blit.srcOffsets[0] = {0, 0, 0}; 
                blit.srcOffsets[1] = {static_cast<int32_t>(srcMipWidth), static_cast<int32_t>(srcMipHeight), 1};
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding), static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding), 0};
                blit.dstOffsets[1] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip), static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip), 1};
                performClampedBlit(blit, VK_FILTER_LINEAR, sourceTextureLoader->getImage());

                // --- Blit Padding (stretch/clamp edges of source mip) ---
                // Top border
                blit.srcOffsets[0] = {0, 0, 0}; blit.srcOffsets[1] = {static_cast<int32_t>(srcMipWidth), 1, 1}; // Top row of src mip
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip), 0};
                blit.dstOffsets[1] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Bottom border
                blit.srcOffsets[0] = {0, static_cast<int32_t>(srcMipHeight - 1), 0}; blit.srcOffsets[1] = {static_cast<int32_t>(srcMipWidth), static_cast<int32_t>(srcMipHeight), 1}; // Bottom row
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip), 0};
                // If this is a bottom-most tile, extend padding to actualAtlasMipHeight
                uint32_t bottomPaddingEndY = (currentTileY == atlasDimInTiles - 1) ? 
                                             actualAtlasMipHeight : 
                                             static_cast<uint32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip + currentMipPadding);
                blit.dstOffsets[1] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip), 
                                      static_cast<int32_t>(bottomPaddingEndY), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Left border
                blit.srcOffsets[0] = {0, 0, 0}; blit.srcOffsets[1] = {1, static_cast<int32_t>(srcMipHeight), 1}; // Left column
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding), 0};
                blit.dstOffsets[1] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Right border
                blit.srcOffsets[0] = {static_cast<int32_t>(srcMipWidth - 1), 0, 0}; blit.srcOffsets[1] = {static_cast<int32_t>(srcMipWidth), static_cast<int32_t>(srcMipHeight), 1}; // Right column
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding), 0};
                // If this is a right-most tile, extend padding to actualAtlasMipWidth
                uint32_t rightPaddingEndX = (currentTileX == atlasDimInTiles - 1) ? 
                                            actualAtlasMipWidth : 
                                            static_cast<uint32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip + currentMipPadding);
                blit.dstOffsets[1] = {static_cast<int32_t>(rightPaddingEndX), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Top-Left Corner
                blit.srcOffsets[0] = {0,0,0}; blit.srcOffsets[1] = {1,1,1}; // Top-left pixel
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip), 0};
                blit.dstOffsets[1] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Top-Right Corner
                blit.srcOffsets[0] = {static_cast<int32_t>(srcMipWidth-1),0,0}; blit.srcOffsets[1] = {static_cast<int32_t>(srcMipWidth),1,1}; // Top-right pixel
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip), 0};
                // Use rightPaddingEndX for the x-component of dstOffsets[1]
                blit.dstOffsets[1] = {static_cast<int32_t>(rightPaddingEndX), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Bottom-Left Corner
                blit.srcOffsets[0] = {0,static_cast<int32_t>(srcMipHeight-1),0}; blit.srcOffsets[1] = {1,static_cast<int32_t>(srcMipHeight),1}; // Bottom-left pixel
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip), 0};
                // Use bottomPaddingEndY for the y-component of dstOffsets[1]
                blit.dstOffsets[1] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding), 
                                      static_cast<int32_t>(bottomPaddingEndY), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());

                // Bottom-Right Corner
                blit.srcOffsets[0] = {static_cast<int32_t>(srcMipWidth-1),static_cast<int32_t>(srcMipHeight-1),0}; blit.srcOffsets[1] = {static_cast<int32_t>(srcMipWidth),static_cast<int32_t>(srcMipHeight),1}; // Bottom-right pixel
                blit.dstOffsets[0] = {static_cast<int32_t>(dstCellOriginX_atlas_mip + currentMipPadding + dstContentSizeThisMip), 
                                      static_cast<int32_t>(dstCellOriginY_atlas_mip + currentMipPadding + dstContentSizeThisMip), 0};
                // Use rightPaddingEndX for x and bottomPaddingEndY for y
                blit.dstOffsets[1] = {static_cast<int32_t>(rightPaddingEndX), 
                                      static_cast<int32_t>(bottomPaddingEndY), 1};
                performClampedBlit(blit, VK_FILTER_NEAREST, sourceTextureLoader->getImage());
                // --- End Padding Blits ---

                // Transition source texture's current mip level back to SHADER_READ_ONLY_OPTIMAL
                ::transitionImageLayout(commandBuffer, sourceTextureLoader->getImage(), sourceTextureLoader->getFormat(),
                                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                           mipLevel, 1, 0, 1);
            }
            // else: source texture doesn't have this many mip levels. Its slot in the atlas for this mip level
            // will remain as it was after the initial transition to TRANSFER_DST_OPTIMAL (likely undefined content).

            // Store/Update AtlasTextureInfo for mip 0 only, as UVs are for the base image.
            if (mipLevel == 0) {
                AtlasTextureInfo info;
                // UVs point to the content area within the padded cell at mip 0.
                // atlasPixelWidth/Height are based on paddedCellSizeMip0.
                float contentOriginU_norm = (static_cast<float>(currentTileX * paddedCellSizeMip0 + ATLAS_PADDING_MIP0) / atlasPixelWidth);
                float contentOriginV_norm = (static_cast<float>(currentTileY * paddedCellSizeMip0 + ATLAS_PADDING_MIP0) / atlasPixelHeight);
                float contentSizeU_norm = (static_cast<float>(maxContentTileSizeMip0) / atlasPixelWidth);
                float contentSizeV_norm = (static_cast<float>(maxContentTileSizeMip0) / atlasPixelHeight);
                
                info.uvOffset = {
                    contentOriginU_norm,
                    contentOriginV_norm
                };
                info.uvScale = {
                    contentSizeU_norm,
                    contentSizeV_norm
                };
                m_texturePathToAtlasInfoMap[texturePath] = info;

                if (texturePath == defaultTexturePath_) {
                    m_defaultAtlasTextureInfo = info;
                }
            }

            currentTileX++;
            if (currentTileX >= atlasDimInTiles) {
                currentTileX = 0;
                currentTileY++;
            }
        }

        // After processing all textures for this mipLevel, transition this atlas mip to SHADER_READ_ONLY_OPTIMAL
        ::transitionImageLayout(commandBuffer, m_textureAtlas->getImage(), VK_FORMAT_R8G8B8A8_SRGB,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                mipLevel, 1, 0, 1);
    }

    VulkanTextureLoader::endSingleTimeCommands(device_, commandPool_, graphicsQueue_, commandBuffer); // Use static method

    // The call to m_textureAtlas->generateMipmaps() is REMOVED.
    // All atlas mips are now individually constructed and in SHADER_READ_ONLY_OPTIMAL.


    // Create a custom sampler for the atlas with LOD clamping
    if (m_textureAtlas && m_textureAtlas->getImage() != VK_NULL_HANDLE) {
        // Destroy the old custom sampler if it exists, before creating a new one
        if (m_customAtlasSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device_, m_customAtlasSampler, nullptr);
            m_customAtlasSampler = VK_NULL_HANDLE; // Good practice to nullify after destruction
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST; // Or VK_FILTER_NEAREST for pixelated look
        samplerInfo.minFilter = VK_FILTER_LINEAR; // Linear for smoother mip transitions
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // Or CLAMP_TO_EDGE, etc.
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        if (properties.limits.maxSamplerAnisotropy > 1.0f) { // Check if physical device supports anisotropy
            samplerInfo.anisotropyEnable = VK_TRUE; 
            samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy; // Use max supported by physical device
        } else {
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0f;
        }
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; // Linear for smoother mip transitions
        samplerInfo.mipLodBias = 0.0f; // Try a small positive bias (e.g., 0.25 to 1.0)
        samplerInfo.minLod = 0.0f;
        // atlasTotalMipLevels was calculated earlier
        float maxLodValue = static_cast<float>(atlasTotalMipLevels - 1);
        if (atlasTotalMipLevels > ATLAS_CLAMP_LOWEST_MIP_LEVELS) { // Ensure we don't underflow
            maxLodValue = static_cast<float>(atlasTotalMipLevels - 1 - ATLAS_CLAMP_LOWEST_MIP_LEVELS);
        }
        samplerInfo.maxLod = std::max(0.0f, maxLodValue); // Clamp if ATLAS_CLAMP_LOWEST_MIP_LEVELS is large
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

// In ResourceManager.hpp, you might need to add getFormat() to VulkanTextureLoader if it's not there
// and ensure imageFormat_ is correctly set/accessible.
// For the anonymous transitionImageLayout, it uses VK_FORMAT_R8G8B8A8_SRGB. If source textures
// can have different formats, this needs to be handled (e.g., by passing sourceTextureLoader->getFormat()).

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