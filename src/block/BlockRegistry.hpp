#ifndef BLOCK_REGISTRY_HPP
#define BLOCK_REGISTRY_HPP

#include <string>
#include <cstdint> // For uint16_t
#include <vector>
#include <map>
#include "Block.hpp" // Your Block class that defines a block type
#include <array>     // For std::array

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
    Block* getBlockDefinitionForModification(uint16_t id); // Added this line
    const std::map<uint16_t, Block>& getAllBlockDefinitions() const;
    
    // Methods to manage face occlusion properties directly in the registry
    void setBlockFaceOcclusion(uint16_t blockID, FaceDirection dir, bool isFull);
    bool isBlockFaceFull(uint16_t blockID, FaceDirection dir) const; // Now directly uses internal cache


private:
    std::map<uint16_t, Block> m_blockDefinitions; // Stores Block objects, keyed by their uint16_t ID
    // Direct cache for face occlusion properties for fast lookups
    std::vector<std::array<bool, 6>> m_blockFaceOcclusionData;
};

#endif // BLOCK_REGISTRY_HPP