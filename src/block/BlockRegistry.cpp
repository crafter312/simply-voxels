#include "BlockRegistry.hpp"
#include <stdexcept> // For std::runtime_error
#include <iostream>  // For std::cout

BlockRegistry::BlockRegistry() {
    std::cout << "BlockRegistry created." << std::endl;
}

void BlockRegistry::registerBlockType(uint16_t id, const std::string& modelPath, const std::string& texturePath) {
    // Using emplace for efficiency. If an ID already exists, this will overwrite the Block definition.
    // You might want to add a check here if overwriting is not desired:
    // if (m_blockDefinitions.count(id)) {
    //     std::cerr << "BlockRegistry Warning: Block ID " << id << " already registered. Overwriting." << std::endl;
    // }
    m_blockDefinitions.emplace(std::piecewise_construct,
                               std::forward_as_tuple(id), // Key for the map
                               std::forward_as_tuple(id, modelPath, texturePath)); // Arguments for Block constructor
    
    // Ensure the occlusion data cache is large enough.
    // New entries in the vector will be value-initialized (std::array<bool, 6> will have all bools as false).
    if (id >= m_blockFaceOcclusionData.size()) {
        m_blockFaceOcclusionData.resize(id + 1);
    }
    std::cout << "BlockRegistry: Registered block with ID " << id << "." << std::endl;
}

const Block* BlockRegistry::getBlockDefinition(uint16_t id) const {
    auto it = m_blockDefinitions.find(id);
    if (it != m_blockDefinitions.end()) {
        return &it->second;
    }
    return nullptr; // Return nullptr if not found
}

Block* BlockRegistry::getBlockDefinitionForModification(uint16_t id) {
    auto it = m_blockDefinitions.find(id);
    if (it != m_blockDefinitions.end()) {
        return &it->second; // Return a pointer to the modifiable Block object
    }
    return nullptr; // Return nullptr if not found
}

const std::map<uint16_t, Block>& BlockRegistry::getAllBlockDefinitions() const {
    return m_blockDefinitions;
}

void BlockRegistry::setBlockFaceOcclusion(uint16_t blockID, FaceDirection dir, bool isFull) {
    // First, ensure the block type is actually registered.
    if (m_blockDefinitions.find(blockID) == m_blockDefinitions.end()) {
        std::cerr << "BlockRegistry Warning: Attempting to set face occlusion for unregistered block ID " << blockID << std::endl;
        return;
    }

    // Ensure the cache is large enough (should have been handled by registerBlockType, but good for safety)
    if (blockID >= m_blockFaceOcclusionData.size()) {
        m_blockFaceOcclusionData.resize(blockID + 1); // New arrays are value-initialized (all bools false)
    }
    m_blockFaceOcclusionData[blockID][static_cast<size_t>(dir)] = isFull;
}

bool BlockRegistry::isBlockFaceFull(uint16_t blockID, FaceDirection dir) const {
    if (blockID < m_blockFaceOcclusionData.size()) {
        // Direct lookup from the cache
        return m_blockFaceOcclusionData[blockID][static_cast<size_t>(dir)];
    }
    // If blockID is out of bounds or was never configured, assume it's not occluding.
    return false;
}