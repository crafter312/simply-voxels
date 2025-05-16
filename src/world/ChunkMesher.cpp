#include "ChunkMesher.hpp"
#include "World.hpp" // For World class definition (to query neighbors)
#include "../resource/ResourceManager.hpp" // For ResourceManager definition
#include "../block/Blocks.hpp" // For Blocks::AIR_ID

#include <array>
#include <iostream> // For debugging

namespace ChunkMesher {

/**
 * @brief Adds the vertices and indices of a given block model to the chunk's mesh data.
 * 
 * The model's vertices are offset by the block's local position within the chunk.
 * Vertex normals and texture coordinates are taken directly from the loaded model.
 * 
 * @param chunkMeshData The mesh data for the entire chunk, to which this block's model will be added.
 * @param blockModelData The pre-loaded model data (vertices, indices, normals, UVs) for the block type.
 * @param blockLocalOffset The integer local coordinates (e.g., 0-15) of this block within the chunk.
 */
void addBlockModelToMeshData(MeshData& chunkMeshData,
                             const ModelData& blockModelData,
                             const glm::vec3& blockLocalOffset,
                             const AtlasTextureInfo& atlasInfo) { // Added atlasInfo parameter
    if (blockModelData.vertices.empty()) { // No need to check indices if no vertices
        // std::cout << "Debug: Block model has no vertices. Skipping." << std::endl;
        return; 
    }
    if (blockModelData.indices.empty()) {
        // std::cout << "Debug: Block model has no indices. Skipping." << std::endl;
        return;
    }

    uint32_t baseVertexIndex = static_cast<uint32_t>(chunkMeshData.vertices.size());

    for (const auto& modelVertex : blockModelData.vertices) {
        Vertex chunkVertex;
        // Offset the model's vertex positions by the block's local position in the chunk
        chunkVertex.pos = modelVertex.pos + blockLocalOffset;
        chunkVertex.normal = modelVertex.normal;     // Use normals from the model
        // Transform texture coordinates to use the atlas
        chunkVertex.texCoord.x = modelVertex.texCoord.x * atlasInfo.uvScale.x + atlasInfo.uvOffset.x;
        chunkVertex.texCoord.y = modelVertex.texCoord.y * atlasInfo.uvScale.y + atlasInfo.uvOffset.y;
        chunkMeshData.vertices.push_back(chunkVertex);
    }

    for (uint32_t modelIndex : blockModelData.indices) {
        chunkMeshData.indices.push_back(baseVertexIndex + modelIndex);
    }
}

MeshData generateMesh(
    glm::ivec3 chunkCoord, // Use passed chunkCoord
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> blockDataSnapshot, // Use snapshot
    bool isChunkAllAir,    // Use passed all-air status
    const World& world, 
    const ResourceManager& resourceManager) {

    MeshData meshData;

    if (isChunkAllAir || !blockDataSnapshot) {
        return meshData; // Empty mesh for all-air or no data chunks
    }

    // Calculate chunk's world origin using its coordinates
    glm::ivec3 chunkWorldOrigin = glm::ivec3(
        chunkCoord.x * CHUNK_WIDTH,
        chunkCoord.y * CHUNK_HEIGHT,
        chunkCoord.z * CHUNK_DEPTH
    );

    for (int i = 0; i < CHUNK_VOLUME; ++i) {
        // Reconstruct local x, y, z from the 1D index 'i'.
        // The order of operations here must correctly invert the Chunk::localToIndex formula:
        // index = x + y * CHUNK_WIDTH + z * CHUNK_WIDTH * CHUNK_HEIGHT
        int x = i % CHUNK_WIDTH;
        int temp_yz_plane_index = i / CHUNK_WIDTH; // This intermediate value is (y + z * CHUNK_HEIGHT)
        int y = temp_yz_plane_index % CHUNK_HEIGHT;
        int z = temp_yz_plane_index / CHUNK_HEIGHT;

        // Get block ID directly using 'i', as it's the correct flattened index for the snapshot array.
        // The x, y, z above are reconstructed to know the 3D position of the block at index 'i'.
        uint16_t blockID = (*blockDataSnapshot)[i];

        if (blockID == Blocks::AIR_ID) {
            continue;
        }

        // For now, assume all non-air blocks are "solid" for culling purposes.
        // You might later add a property to blocks (e.g., isOpaque) from ResourceManager.
        // bool isCurrentBlockOpaque = resourceManager.isBlockOpaque(blockID);
        // if (!isCurrentBlockOpaque) { /* Potentially handle transparent blocks differently or always add them */ }

        // Now, currentBlockLocalPos_ivec and currentBlockLocalPos_vec3 use the correctly reconstructed x, y, z
        glm::ivec3 currentBlockLocalPos_ivec(x, y, z);
        glm::vec3 currentBlockLocalPos_vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        glm::ivec3 currentBlockWorldPos = chunkWorldOrigin + currentBlockLocalPos_ivec;

        // Check if this block is exposed to air (or a non-opaque neighbor)
        bool isExposed = false;
        glm::ivec3 neighborOffsets[] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        };
        // Use NEIGHBOR_OFFSETS from World.hpp for consistency with the array index
        // Ensure NEIGHBOR_OFFSETS is accessible here, or copy it.
        // Assuming NEIGHBOR_OFFSETS is globally accessible or included via World.hpp

        for (int neighborIndex = 0; neighborIndex < 6; ++neighborIndex) {
            glm::ivec3 neighborPos = currentBlockWorldPos + neighborOffsets[neighborIndex];            
            bool isNeighborEffectivelyOpaque;
            glm::ivec3 neighborChunkCoord = World::worldToChunkCoordinates(neighborPos); // Get chunk coords of neighbor

            World::ChunkGenStatus neighborStatus = world.getChunkGeneratedStatus(neighborChunkCoord);

            if (neighborStatus == World::ChunkGenStatus::NOT_FOUND ||
                neighborStatus == World::ChunkGenStatus::LOADED_NOT_GENERATED) {
                // Treat as opaque if neighbor chunk is not found,
                // or if it's found but not yet fully generated.
                // This prevents rendering faces against chunks whose state is unknown or incomplete.
                isNeighborEffectivelyOpaque = true;
            } else { // Neighbor is LOADED_AND_GENERATED
                uint16_t neighborBlockID = world.getBlockID(neighborPos); // This is now safe and will get the real ID
                // For simplicity, assume neighbor is non-opaque if it's air.
                // bool isNeighborOpaque = resourceManager.isBlockOpaque(neighborBlockID); // Future improvement
                isNeighborEffectivelyOpaque = (neighborBlockID != Blocks::AIR_ID); // Current placeholder
            }

            if (!isNeighborEffectivelyOpaque) { 
                isExposed = true;
                // The detailed logging for exposed faces can be updated if needed.
                // For example, to confirm the neighborStatus was LOADED_AND_GENERATED and neighborBlockID was AIR.
                break; // Found an exposed side, no need to check others
            }
        }

        if (isExposed) {
            std::shared_ptr<const ModelData> modelDataPtr = resourceManager.getModelForBlockType(blockID);
            AtlasTextureInfo atlasInfo = resourceManager.getBlockAtlasInfo(blockID); // Get atlas info
            if (modelDataPtr) { // Check if the model data was successfully loaded
                addBlockModelToMeshData(meshData, *modelDataPtr, currentBlockLocalPos_vec3, atlasInfo); // Pass atlas info
            }
        }
    }
    return meshData;
}

} // namespace ChunkMesher