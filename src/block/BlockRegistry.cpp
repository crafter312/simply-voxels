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

bool BlockRegistry::isBlockFaceFull(uint16_t blockID, FaceDirection dir) const {
    const Block* blockDef = getBlockDefinition(blockID);
    if (blockDef) {
        return blockDef->hasFullOccludingFace(dir);
    }
    // If block ID is not found (e.g., it's implicitly air or an undefined block),
    // it does not have a full occluding face.
    return false;
}