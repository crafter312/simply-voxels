#include "BlockRegistry.hpp"
#include <stdexcept> // For std::runtime_error
#include <iostream>  // For std::cout

BlockRegistry::BlockRegistry() {
    std::cout << "BlockRegistry created." << std::endl;
}

void BlockRegistry::registerBlockType(const std::string& typeName, const std::string& modelPath, const std::string& texturePath) {
    if (typeName.empty()) {
        std::cerr << "BlockRegistry Warning: Attempted to register a block type with an empty name. Ignoring." << std::endl;
        return;
    }
    // Using emplace for efficiency. If typeName already exists, this will overwrite.
    m_blockDefinitions.emplace(std::piecewise_construct,
                               std::forward_as_tuple(typeName),
                               std::forward_as_tuple(typeName, modelPath, texturePath));
    std::cout << "BlockRegistry: Registered block type '" << typeName << "'." << std::endl;
}

const Block* BlockRegistry::getBlockDefinition(const std::string& typeName) const {
    auto it = m_blockDefinitions.find(typeName);
    if (it != m_blockDefinitions.end()) {
        return &it->second;
    }
    return nullptr; // Return nullptr if not found
}

const std::map<std::string, Block>& BlockRegistry::getAllBlockDefinitions() const {
    return m_blockDefinitions;
}