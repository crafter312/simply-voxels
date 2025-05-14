#ifndef BLOCK_REGISTRY_HPP
#define BLOCK_REGISTRY_HPP

#include <string>
#include <cstdint> // For uint16_t
#include <vector>
#include <map>
#include "Block.hpp" // Your Block class that defines a block type

/**
 * @class BlockRegistry
 * @brief Manages the definitions of all available block types in the game.
 *
 * This class holds unique instances of Block objects, each representing a
 * distinct block type (identified by a uint16_t ID) with its associated
 * model and texture paths.
 */
class BlockRegistry {
public:
    BlockRegistry();

    void registerBlockType(uint16_t id, const std::string& modelPath, const std::string& texturePath);
    const Block* getBlockDefinition(uint16_t id) const;
    const std::map<uint16_t, Block>& getAllBlockDefinitions() const;

private:
    std::map<uint16_t, Block> m_blockDefinitions; // Stores Block objects, keyed by their uint16_t ID
};

#endif // BLOCK_REGISTRY_HPP