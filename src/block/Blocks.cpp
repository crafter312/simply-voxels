#include "Blocks.hpp"
#include "BlockRegistry.hpp" // To call registry.registerBlockType

namespace Blocks {

void registerBlockTypes(BlockRegistry& registry) {
    // Register Dirt
    registry.registerBlockType(DIRT_ID, 
                               CUBE_MODEL_PATH, 
                               "../resources/textures/dirt.png");

    // Register Stone
    registry.registerBlockType(STONE_ID, 
                               CUBE_MODEL_PATH, 
                               "../resources/textures/stone.png");
}

} // namespace Blocks