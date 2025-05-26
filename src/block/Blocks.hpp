#ifndef BLOCKS_HPP
#define BLOCKS_HPP

#include <cstdint> // For uint16_t

// Forward declaration
class BlockRegistry;

namespace Blocks {

// Define constant block IDs
constexpr const char* CUBE_MODEL_PATH = "../resources/models/cube.glb";
constexpr const char* HALF_SLAB_BOTTOM_MODEL_PATH = "../resources/models/half_slab_bottom.glb";
constexpr const char* HALF_SLAB_TOP_MODEL_PATH = "../resources/models/half_slab_top.glb";

// Define constant block IDs
constexpr uint16_t AIR_ID = 0;   // AIR_ID remains 0 and is fundamental.
extern const uint16_t DIRT_ID;
extern const uint16_t STONE_ID;
extern const uint16_t COBBLESTONE_ID;
extern const uint16_t COBBLESTONE_SLAB_ID;

// Function to register all predefined block types
// Takes a reference to a BlockRegistry object to populate it.
void registerBlockTypes(BlockRegistry& registry);

} // namespace Blocks

#endif // BLOCKS_HPP