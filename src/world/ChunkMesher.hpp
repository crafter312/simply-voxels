#ifndef CHUNK_MESHER_HPP
#define CHUNK_MESHER_HPP

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <array>  // For std::array
#include "../resource/ModelLoader.hpp" // Include for the Vertex struct

// Forward declarations to avoid circular dependencies
// Chunk.hpp is now included directly for CHUNK_VOLUME
#include "Chunk.hpp" // Include Chunk.hpp for CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH, CHUNK_VOLUME
struct AtlasTextureInfo; // Forward declare from ResourceManager.hpp (or include ResourceManager.hpp if not too heavy)
class World;
class ResourceManager; // Assuming ResourceManager provides block properties and texture atlas info
class BlockRegistry;   // Forward declare BlockRegistry
struct ModelData;      // Forward declaration for ModelData

namespace ChunkMesher {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

/**
 * @brief Generates renderable mesh data for a given chunk.
 *
 * This function iterates through the blocks in the chunk, performs face culling
 * against neighboring blocks (both within the chunk and in adjacent chunks via the World object),
 * and adds the model data for exposed blocks.
 * 
 * @param chunkCoord The coordinates of the chunk being meshed.
 * @param blockDataSnapshot A snapshot of the chunk's block data.
 * @param isChunkAllAir True if the chunk is known to be all air.
 * @param world The World object, used to query blocks in neighboring chunks for face culling.
 * @param resourceManager The ResourceManager, used to get block properties and texture information.
 * @param blockRegistry The BlockRegistry, used to query block face properties.
 * @return MeshData A struct containing the generated vertices and indices.
 */
MeshData generateMesh(
    glm::ivec3 chunkCoord,
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> blockDataSnapshot,
    bool isChunkAllAir,
    const World& world,
    const ResourceManager& resourceManager,
    const BlockRegistry& blockRegistry); // Added blockRegistry

// Function to add an entire block's model to the chunk's mesh data
void addBlockModelToMeshData(MeshData& chunkMeshData,
                             const ModelData& blockModelData, // ModelData struct should be defined (e.g. via ModelLoader.hpp)
                             const glm::vec3& blockLocalOffset,
                             const AtlasTextureInfo& atlasInfo); // Added atlas info

} // namespace ChunkMesher

#endif // CHUNK_MESHER_HPP