#include "ChunkMesher.hpp"
#include "Chunk.hpp" // For CHUNK_WIDTH, etc., and Chunk class definition
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

MeshData generateMesh(const Chunk& chunk, const World& world, const ResourceManager& resourceManager) {
    MeshData meshData;
    glm::ivec3 chunkWorldOrigin = chunk.getWorldPosition(); // Base world position of the chunk

    for (int i = 0; i < CHUNK_VOLUME; ++i) {
        // Derive x, y, z from the single loop index i
        // The order of derivation matches the original loop order: y (outer), z (middle), x (inner)
        int x = i % CHUNK_WIDTH;
        int z_temp = i / CHUNK_WIDTH;
        int z = z_temp % CHUNK_DEPTH;
        int y = z_temp / CHUNK_DEPTH;

        uint16_t blockID = chunk.getBlock(x, y, z);

        if (blockID == Blocks::AIR_ID) {
            continue;
        }

        // For now, assume all non-air blocks are "solid" for culling purposes.
        // You might later add a property to blocks (e.g., isOpaque) from ResourceManager.
        // bool isCurrentBlockOpaque = resourceManager.isBlockOpaque(blockID);
        // if (!isCurrentBlockOpaque) { /* Potentially handle transparent blocks differently or always add them */ }

        glm::ivec3 currentBlockLocalPos_ivec(x, y, z);
        glm::vec3 currentBlockLocalPos_vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        glm::ivec3 currentBlockWorldPos = chunkWorldOrigin + currentBlockLocalPos_ivec;

        // Check if this block is exposed to air (or a non-opaque neighbor)
        bool isExposed = false;
        glm::ivec3 neighborOffsets[] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        };

        for (int neighborIndex = 0; neighborIndex < 6; ++neighborIndex) {
            glm::ivec3 neighborPos = currentBlockWorldPos + neighborOffsets[neighborIndex];
            uint16_t neighborBlockID = world.getBlockID(neighborPos);

            // For simplicity, assume neighbor is non-opaque if it's air.
            // bool isNeighborOpaque = resourceManager.isBlockOpaque(neighborBlockID);
            bool isNeighborOpaque = (neighborBlockID != Blocks::AIR_ID); // Placeholder

            if (!isNeighborOpaque) {
                isExposed = true;
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