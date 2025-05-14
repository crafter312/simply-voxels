#ifndef CHUNK_MESHER_HPP
#define CHUNK_MESHER_HPP

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "../resource/ModelLoader.hpp" // Include for the Vertex struct

// Forward declarations to avoid circular dependencies
// These classes will need to be fully included in ChunkMesher.cpp
class Chunk;
struct AtlasTextureInfo; // Forward declare from ResourceManager.hpp (or include ResourceManager.hpp if not too heavy)
class World;
class ResourceManager; // Assuming ResourceManager provides block properties and texture atlas info
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
 * It assumes that the ResourceManager can provide:
 * 1. Whether a block ID is considered "opaque" for culling purposes.
 * 2. The ModelData for any given block ID.
 *
 * @param chunk The Chunk object containing the block data to be meshed.
 * @param world The World object, used to query blocks in neighboring chunks for face culling.
 * @param resourceManager The ResourceManager, used to get block properties and texture information.
 * @return MeshData A struct containing the generated vertices and indices.
 */
MeshData generateMesh(const Chunk& chunk, const World& world, const ResourceManager& resourceManager);

// Function to add an entire block's model to the chunk's mesh data
void addBlockModelToMeshData(MeshData& chunkMeshData,
                             const ModelData& blockModelData, // ModelData struct should be defined (e.g. via ModelLoader.hpp)
                             const glm::vec3& blockLocalOffset,
                             const AtlasTextureInfo& atlasInfo); // Added atlas info

} // namespace ChunkMesher

#endif // CHUNK_MESHER_HPP