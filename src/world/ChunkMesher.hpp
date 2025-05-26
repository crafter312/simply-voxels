#ifndef CHUNK_MESHER_HPP
#define CHUNK_MESHER_HPP

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <array>       // For std::array
#include <memory>      // For std::unique_ptr

// Forward declarations to avoid circular dependencies
// Chunk.hpp is now included directly for CHUNK_VOLUME
#include "Chunk.hpp" // Include Chunk.hpp for CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH, CHUNK_VOLUME
// Include ModelData definition, which now contains Vertex and AtlasTextureInfo (via forward declaration or direct definition if moved)
#include "../resource/ModelLoader.hpp" 

class World;
class ResourceManager; // Assuming ResourceManager provides block properties and texture atlas info
class BlockRegistry;   // Forward declare BlockRegistry
// ModelData is now included via ModelLoader.hpp

namespace ChunkMesher {

/**
 * @brief Generates renderable mesh data for a given chunk.
 *
 * This function iterates through the blocks in the chunk, performs face culling
 * against neighboring blocks (both within the chunk and in adjacent chunks via the World object),
 * and adds the model data for exposed blocks.
 * 
 * @param chunkCoord The coordinates of the chunk being meshed.
 * @param currentChunk The actual Chunk object to be meshed.
 * @param world The World object, used to query blocks in neighboring chunks for face culling.
 * @param resourceManager The ResourceManager, used to get block properties and texture information.
 * @param blockRegistry The BlockRegistry, used to query block face properties.
 * @return MeshData A struct containing the generated vertices and indices.
 * @return ModelData A struct containing the generated vertices, indices, and per-vertex atlas info.
 */

    // Helper to define offsets to neighbor chunks
    const std::array<glm::ivec3, 6> NEIGHBOR_OFFSETS = {{
        {1, 0, 0}, {-1, 0, 0},  // Right, Left
        {0, 1, 0}, {0, -1, 0},  // Up, Down
        {0, 0, 1}, {0, 0, -1}   // Front, Back
    }};
ModelData generateMesh(
    glm::ivec3 chunkCoord, // Still useful for neighbor lookups and context
    const Chunk& currentChunk,
    const World& world,
    const ResourceManager& resourceManager,
    const BlockRegistry& blockRegistry); // Added blockRegistry

// Function to add an entire block's model to the chunk's mesh data
void addBlockModelToMeshData(ModelData& chunkMeshData,
                             const ModelData& blockModelData, // ModelData struct should be defined (e.g. via ModelLoader.hpp)
                             const glm::vec3& blockLocalOffset); // AtlasInfo is now part of blockModelData

} // namespace ChunkMesher

#endif // CHUNK_MESHER_HPP