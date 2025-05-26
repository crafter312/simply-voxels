#include "Blocks.hpp"
#include "BlockRegistry.hpp" // To call registry.registerBlockType

namespace Blocks {

// Static counter to assign block IDs sequentially after AIR_ID.
static uint16_t s_nextAvailableBlockID = 1; // Start from 1, as AIR_ID is 0.

const uint16_t DIRT_ID = s_nextAvailableBlockID++;
const uint16_t STONE_ID = s_nextAvailableBlockID++;
const uint16_t COBBLESTONE_ID = s_nextAvailableBlockID++;
const uint16_t COBBLESTONE_SLAB_ID = s_nextAvailableBlockID++;

void registerBlockTypes(BlockRegistry& registry) {
    // Register Dirt
    registry.registerBlockType(DIRT_ID, 
                               CUBE_MODEL_PATH, 
                               "../resources/textures/dirt.png");

    // Register Stone
    registry.registerBlockType(STONE_ID, 
                               CUBE_MODEL_PATH, 
                               "../resources/textures/stone.png");

    // Register Cobblestone
    registry.registerBlockType(COBBLESTONE_ID, 
                               CUBE_MODEL_PATH, 
                               "../resources/textures/cobblestone.png");

    // Register Cobblestone Slab
    registry.registerBlockType(COBBLESTONE_SLAB_ID, 
                               HALF_SLAB_BOTTOM_MODEL_PATH, 
                               "../resources/textures/cobblestone.png");
    registry.getBlockDefinitionForModification(COBBLESTONE_SLAB_ID)->setCustomShape(
        Physics::VoxelShape::createCuboidShape(0, 0, 0, 16, 8, 16)
    );
}

} // namespace Blocks