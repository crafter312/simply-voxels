#include "ResourceManager.hpp"
#include <iostream> // For error messages/logging
#include <stdexcept> // For std::runtime_error
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
    // loadedModels_ will clean up ModelData instances (which are just CPU data)
    std::cout << "ResourceManager destroyed." << std::endl;
}

void ResourceManager::loadAssetsFromRegistry(const BlockRegistry& registry, bool preLoadAll) {
    std::cout << "ResourceManager: Loading assets from block registry..." << std::endl;
    const auto& allDefinitions = registry.getAllBlockDefinitions();

    for (const auto& pair : allDefinitions) {
        const Block& blockDef = pair.second; // blockDef is a Block object from BlockRegistry
        
        // Store a copy of the Block object (which contains the paths) in our map
        blockAssetPaths_.emplace(blockDef.getTypeName(), blockDef);
        std::cout << "ResourceManager: Registered asset paths for block type '" << blockDef.getTypeName() 
                  << "' (Model: '" << blockDef.getModelPath() << "', Texture: '" << blockDef.getTexturePath() << "')" << std::endl;

        if (preLoadAll) {
            std::cout << "ResourceManager: Pre-loading assets for '" << blockDef.getTypeName() << "'." << std::endl;
            if (!blockDef.getModelPath().empty()) internalLoadModel(blockDef.getModelPath());
            if (!blockDef.getTexturePath().empty()) internalLoadTexture(blockDef.getTexturePath());
        }
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

const ModelData& ResourceManager::internalLoadModel(const std::string& path, bool isFallbackAttempt) {
    if (path.empty()) {
        throw std::runtime_error("ResourceManager: Attempted to load model with an empty path.");
    }

    auto it = loadedModels_.find(path);
    if (it != loadedModels_.end()) {
        return it->second;
    }

    std::cout << "ResourceManager: Loading model: " << path << std::endl;
    ModelData newModelData;
    if (!ModelLoader::loadGltfModel(path, newModelData)) {
        std::cerr << "ResourceManager Error: Failed to load model: " << path << std::endl;
        if (!isFallbackAttempt && !defaultModelPath_.empty() && path != defaultModelPath_) {
            std::cout << "ResourceManager: Attempting to load default model: " << defaultModelPath_ << std::endl;
            return internalLoadModel(defaultModelPath_, true); // True to prevent infinite recursion on default failing
        }
        throw std::runtime_error("ResourceManager: Critical failure loading model '" + path + "' and default model unavailable or also failed.");
    }
    auto [inserted_it, success] = loadedModels_.emplace(path, std::move(newModelData));
    return inserted_it->second;
}

VulkanTextureLoader* ResourceManager::internalLoadTexture(const std::string& path, bool isFallbackAttempt) {
    if (path.empty()) {
        throw std::runtime_error("ResourceManager: Attempted to load texture with an empty path.");
    }

    auto it = loadedTextures_.find(path);
    if (it != loadedTextures_.end()) {
        return it->second.get();
    }

    std::cout << "ResourceManager: Loading texture: " << path << std::endl;
    try {
        auto newTextureLoader = std::make_unique<VulkanTextureLoader>(physicalDevice_, device_, commandPool_, graphicsQueue_, path);
        VulkanTextureLoader* ptr = newTextureLoader.get();
        loadedTextures_[path] = std::move(newTextureLoader);
        return ptr;
    } catch (const std::runtime_error& e) {
        std::cerr << "ResourceManager Error: Failed to load texture '" << path << "': " << e.what() << std::endl;
        if (!isFallbackAttempt && !defaultTexturePath_.empty() && path != defaultTexturePath_) {
            std::cout << "ResourceManager: Attempting to load default texture: " << defaultTexturePath_ << std::endl;
            return internalLoadTexture(defaultTexturePath_, true); // True to prevent infinite recursion
        }
        throw std::runtime_error("ResourceManager: Critical failure loading texture '" + path + "' and default texture unavailable or also failed.");
    }
}

const ModelData& ResourceManager::getModelForBlockType(const std::string& typeName) {
    auto it = blockAssetPaths_.find(typeName);
    if (it != blockAssetPaths_.end()) {
        const Block& blockDefinition = it->second; // Get the Block object containing paths
        if (!blockDefinition.getModelPath().empty()) {
            return internalLoadModel(blockDefinition.getModelPath());
        }
        // If model path is empty for this specific type, fall through to default
        std::cout << "ResourceManager: Block type '" << typeName << "' has an empty model path. Using default model." << std::endl;
    } else {
        // Type name not found in our asset path map
        std::cerr << "ResourceManager Warning: No asset paths registered for block type '" << typeName << "'. Using default model." << std::endl;
    }
    return internalLoadModel(defaultModelPath_);
}

VulkanTextureLoader* ResourceManager::getTextureForBlockType(const std::string& typeName) {
    auto it = blockAssetPaths_.find(typeName);
    if (it != blockAssetPaths_.end()) {
        const Block& blockDefinition = it->second; // Get the Block object containing paths
        if (!blockDefinition.getTexturePath().empty()) {
            return internalLoadTexture(blockDefinition.getTexturePath());
        }
        std::cout << "ResourceManager: Block type '" << typeName << "' has an empty texture path. Using default texture." << std::endl;
    } else {
        std::cerr << "ResourceManager Warning: No asset paths registered for block type '" << typeName << "'. Using default texture." << std::endl;
    }
    return internalLoadTexture(defaultTexturePath_);
}