#ifndef BLOCK_REGISTRY_HPP
#define BLOCK_REGISTRY_HPP

#include <string>
#include <vector>
#include <map>
#include "Block.hpp" // Your Block class that defines a block type

/**
 * @class BlockRegistry
 * @brief Manages the definitions of all available block types in the game.
 *
 * This class holds unique instances of Block objects, each representing a distinct
 * block type with its associated model and texture paths.
 */
class BlockRegistry {
public:
    BlockRegistry();

    void registerBlockType(const std::string& typeName, const std::string& modelPath, const std::string& texturePath);
    const Block* getBlockDefinition(const std::string& typeName) const;
    const std::map<std::string, Block>& getAllBlockDefinitions() const;

private:
    std::map<std::string, Block> m_blockDefinitions; // Stores Block objects, keyed by type name
};

#endif // BLOCK_REGISTRY_HPP