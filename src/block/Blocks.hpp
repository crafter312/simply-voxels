#ifndef BLOCKS_HPP
#define BLOCKS_HPP

#include <cstdint> // For uint16_t

// Forward declaration
class BlockRegistry;

namespace Blocks {

// Define constant block IDs
constexpr const char* CUBE_MODEL_PATH = "../resources/models/cube.glb";

// Define constant block IDs
constexpr uint16_t AIR_ID = 0;   // Matches AIR_BLOCK_ID in Chunk.hpp
constexpr uint16_t DIRT_ID = 1;  // Matches DIRT_BLOCK_ID in World.cpp
constexpr uint16_t STONE_ID = 2; // New ID for Stone

// Function to register all predefined block types
// Takes a reference to a BlockRegistry object to populate it.
void registerBlockTypes(BlockRegistry& registry);

} // namespace Blocks

#endif // BLOCKS_HPP