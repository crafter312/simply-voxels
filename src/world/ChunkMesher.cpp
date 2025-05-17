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

// Helper struct to cache data for neighbor chunks
struct CachedNeighborData {
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> blockDataSnapshot = nullptr;
    bool isAllAir = true; // Default to true, meaning if not found or not generated, effectively air for culling
    bool isGeneratedAndExists = false; // True if the chunk exists and has finished generation
};

MeshData generateMesh(
    glm::ivec3 chunkCoord, // Use passed chunkCoord
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> currentChunkBlockDataSnapshot, // Renamed for clarity
    bool isCurrentChunkAllAir,    // Renamed for clarity
    const World& world, 
    const ResourceManager& resourceManager) {

    MeshData meshData;

    if (isCurrentChunkAllAir || !currentChunkBlockDataSnapshot) {
        return meshData; // Empty mesh for all-air or no data chunks
    }

    // Calculate chunk's world origin using its coordinates
    glm::ivec3 chunkWorldOrigin = glm::ivec3(
        chunkCoord.x * CHUNK_WIDTH,
        chunkCoord.y * CHUNK_HEIGHT,
        chunkCoord.z * CHUNK_DEPTH
    );

    // 1. Cache data for 6 adjacent neighbor chunks
    std::array<CachedNeighborData, 6> neighborCache;
    for (int i = 0; i < 6; ++i) { // NEIGHBOR_OFFSETS has 6 elements
        glm::ivec3 neighborChunkGlobalCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
        const Chunk* neighborChunkPtr = world.getChunk(neighborChunkGlobalCoord);

        if (neighborChunkPtr && neighborChunkPtr->isGenerated()) {
            neighborCache[i].isGeneratedAndExists = true;
            neighborCache[i].isAllAir = neighborChunkPtr->isAllAir(); // Store this first
            if (!neighborCache[i].isAllAir) { // Only get snapshot if not all air
                 neighborCache[i].blockDataSnapshot = neighborChunkPtr->getBlockDataSnapshot();
                 // If snapshot is null even if not allAir, it means it became allAir very recently
                 // or there's an issue. isAllAir flag should be primary.
                 if (!neighborCache[i].blockDataSnapshot) neighborCache[i].isAllAir = true;
            }
        } else {
            neighborCache[i].isGeneratedAndExists = false;
            // Defaults (isAllAir = true, blockDataSnapshot = nullptr) are fine.
        }
    }

    // 2. Iterate through blocks in the current chunk using nested loops
    for (int z = 0; z < CHUNK_DEPTH; ++z) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int x = 0; x < CHUNK_WIDTH; ++x) {
                size_t blockIndex = Chunk::localToIndex(x, y, z);
                uint16_t blockID = (*currentChunkBlockDataSnapshot)[blockIndex];

                if (blockID == Blocks::AIR_ID) {
                    continue;
                }

                glm::ivec3 currentBlockLocalPos_ivec(x, y, z);
                bool isExposed = false;

                // 3. Check 6 faces for exposure
                for (int faceIndex = 0; faceIndex < 6; ++faceIndex) { // Iterate 6 faces
                    glm::ivec3 neighborOffset = NEIGHBOR_OFFSETS[faceIndex];
                    glm::ivec3 neighborLocalPosRelativeToCurrentChunk = currentBlockLocalPos_ivec + neighborOffset;

                    uint16_t neighborBlockID_val;
                    bool neighborIsOpaque;

                    // Check if neighbor is within the current chunk
                    if (neighborLocalPosRelativeToCurrentChunk.x >= 0 && neighborLocalPosRelativeToCurrentChunk.x < CHUNK_WIDTH &&
                        neighborLocalPosRelativeToCurrentChunk.y >= 0 && neighborLocalPosRelativeToCurrentChunk.y < CHUNK_HEIGHT &&
                        neighborLocalPosRelativeToCurrentChunk.z >= 0 && neighborLocalPosRelativeToCurrentChunk.z < CHUNK_DEPTH) {
                        
                        size_t neighborIdxInSnapshot = Chunk::localToIndex(
                            neighborLocalPosRelativeToCurrentChunk.x,
                            neighborLocalPosRelativeToCurrentChunk.y,
                            neighborLocalPosRelativeToCurrentChunk.z
                        );
                        neighborBlockID_val = (*currentChunkBlockDataSnapshot)[neighborIdxInSnapshot];
                        // Basic opacity: non-air is opaque. Extend with resourceManager.isBlockOpaque(neighborBlockID_val) if needed.
                        neighborIsOpaque = (neighborBlockID_val != Blocks::AIR_ID);
                    } else {
                        // Neighbor is in an adjacent chunk, use the cache
                        const CachedNeighborData& cachedNeighbor = neighborCache[faceIndex];

                        if (!cachedNeighbor.isGeneratedAndExists) {
                            neighborIsOpaque = true; // Treat as opaque if neighbor chunk is not ready/non-existent
                        } else {
                            if (cachedNeighbor.isAllAir) {
                                neighborBlockID_val = Blocks::AIR_ID;
                            } else {
                                // Calculate local coordinates within the *neighbor* chunk
                                // (coord + CHUNK_DIM) % CHUNK_DIM handles negative results from subtraction correctly for modulo.
                                glm::ivec3 localPosInNeighborChunk(
                                    (currentBlockLocalPos_ivec.x + neighborOffset.x + CHUNK_WIDTH) % CHUNK_WIDTH,
                                    (currentBlockLocalPos_ivec.y + neighborOffset.y + CHUNK_HEIGHT) % CHUNK_HEIGHT,
                                    (currentBlockLocalPos_ivec.z + neighborOffset.z + CHUNK_DEPTH) % CHUNK_DEPTH
                                );
                                
                                if (cachedNeighbor.blockDataSnapshot) { // Check if snapshot was actually retrieved
                                    neighborBlockID_val = (*cachedNeighbor.blockDataSnapshot)[Chunk::localToIndex(
                                        localPosInNeighborChunk.x,
                                        localPosInNeighborChunk.y,
                                        localPosInNeighborChunk.z
                                    )];
                                } else {
                                    // Should be caught by isAllAir, but as a fallback:
                                    neighborBlockID_val = Blocks::AIR_ID; 
                                }
                            }
                            neighborIsOpaque = (neighborBlockID_val != Blocks::AIR_ID);
                        }
                    }

                    if (!neighborIsOpaque) {
                        isExposed = true;
                        break; // Found an exposed face, no need to check other faces for this block
                    }
                } // End face check loop

                if (isExposed) {
                    std::shared_ptr<const ModelData> modelDataPtr = resourceManager.getModelForBlockType(blockID);
                    AtlasTextureInfo atlasInfo = resourceManager.getBlockAtlasInfo(blockID); // Get atlas info
                    if (modelDataPtr) { // Check if the model data was successfully loaded
                        // Convert ivec3 to vec3 for addBlockModelToMeshData
                        glm::vec3 currentBlockLocalPos_vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                        addBlockModelToMeshData(meshData, *modelDataPtr, currentBlockLocalPos_vec3, atlasInfo); // Pass atlas info
                    }
                }
            }
        } // End y loop
    } // End z loop

    return meshData;
}

} // namespace ChunkMesher