#include "ChunkMesher.hpp"
#include "World.hpp" // For World class definition (to query neighbors)
#include "../resource/ResourceManager.hpp" // For ResourceManager definition
#include "../block/Blocks.hpp" // For Blocks::AIR_ID
#include "../block/BlockRegistry.hpp" // Include for BlockRegistry type

#include <array>
#include <iostream> // For debugging
#include <optional>  // For std::optional

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
void addBlockModelToMeshData(ModelData& chunkMeshData,
                             const ModelData& blockModelData,
                             const glm::vec3& blockLocalOffset) { // Removed atlasInfo parameter
    if (blockModelData.vertices.empty()) { // No need to check indices if no vertices
        // std::cout << "Debug: Block model has no vertices. Skipping." << std::endl;
        return; 
    }
    if (blockModelData.indices.empty()) {
        // std::cout << "Debug: Block model has no indices. Skipping." << std::endl;
        return;
    }

    uint32_t baseVertexIndex = static_cast<uint32_t>(chunkMeshData.vertices.size());

    for (size_t i = 0; i < blockModelData.vertices.size(); ++i) {
        const auto& modelVertex = blockModelData.vertices[i];
        // const auto& modelAtlasInfo = blockModelData.atlasTextureInfos[i]; // This line is no longer needed as ModelData doesn't have this vector

        Vertex chunkVertex;
        // Offset the model's vertex positions by the block's local position in the chunk
        chunkVertex.pos = modelVertex.pos + blockLocalOffset;
        chunkVertex.normal = modelVertex.normal;     // Use normals from the model
        // Store the original local [0,1] texture coordinates.
        // The transformation using atlas offset/scale will happen in the shader via per-vertex AtlasTextureInfo.
        chunkVertex.texCoord = modelVertex.texCoord;
        // The atlasUvOffset and atlasUvScale are now part of modelVertex itself,
        // so they are implicitly copied when modelVertex is used to construct/assign to chunkVertex,
        // or explicitly copied if needed (as shown below, which is good for clarity).
        chunkVertex.atlasUvOffset = modelVertex.atlasUvOffset;
        chunkVertex.atlasUvScale = modelVertex.atlasUvScale;
        chunkMeshData.vertices.push_back(chunkVertex);
    }

    for (uint32_t modelIndex : blockModelData.indices) {
        chunkMeshData.indices.push_back(baseVertexIndex + modelIndex);
    }
}

// Helper struct to cache data for neighbor chunks
struct CachedNeighborData {
    std::optional<std::array<uint16_t, CHUNK_VOLUME>> blockData = std::nullopt;
    bool isAllAir = true; // Default to true, meaning if not found or not generated, effectively air for culling
    bool isGeneratedAndExists = false; // True if the chunk exists and has finished generation
};

ModelData generateMesh(
    glm::ivec3 chunkCoord, // Chunk coordinates of currentChunk
    const Chunk& currentChunk,
    const World& world,
    const ResourceManager& resourceManager,
    const BlockRegistry& blockRegistry) { // Added blockRegistry
    ModelData meshData;

    if (currentChunk.isAllAir()) {
        return meshData; // Empty mesh for all-air or no data chunks
    }

    // 1. Cache data for 6 adjacent neighbor chunks
    std::array<CachedNeighborData, 6> neighborCache;
    for (int i = 0; i < 6; ++i) { // NEIGHBOR_OFFSETS has 6 elements
        glm::ivec3 neighborChunkGlobalCoord = currentChunk.getChunkCoord() + NEIGHBOR_OFFSETS[i];
        std::shared_ptr<const Chunk> neighborChunk_sptr = world.getChunk(neighborChunkGlobalCoord); // Changed to shared_ptr

        if (neighborChunk_sptr && neighborChunk_sptr->isGenerated()) {
            neighborCache[i].isGeneratedAndExists = true;
            neighborCache[i].isAllAir = neighborChunk_sptr->isAllAir(); // Store this first
            if (!neighborCache[i].isAllAir) { // Only get snapshot if not all air
                neighborChunk_sptr->performLockedRead( // Use shared_ptr here
                    [&](const uint16_t* neighbor_block_data_ptr, size_t capacity) {
                        if (neighbor_block_data_ptr && capacity == CHUNK_VOLUME) {
                            neighborCache[i].blockData.emplace(); // Create the array
                            std::memcpy(neighborCache[i].blockData->data(), neighbor_block_data_ptr, CHUNK_VOLUME * sizeof(uint16_t));
                        } else {
                            // If data is null or capacity mismatch, treat as all air for safety
                            neighborCache[i].isAllAir = true;
                            neighborCache[i].blockData = std::nullopt;
                        }
                    }
                );
            }
        } else {
            neighborCache[i].isGeneratedAndExists = false;
            // Defaults (isAllAir = true, blockData = std::nullopt) are fine.
        }
    }

    // 2. Perform locked read on the current chunk and generate its mesh
    currentChunk.performLockedRead(
        [&](const uint16_t* currentChunkBlocks_ptr, size_t capacity) {
            if (!currentChunkBlocks_ptr || capacity != CHUNK_VOLUME) {
                // This case should ideally be caught by the initial currentChunk.isAllAir() check,
                // but as a safeguard, if no valid block data is provided, we can't mesh.
                return; // meshData remains empty
            }

            // Iterate through blocks in the current chunk using nested loops
            for (int z = 0; z < CHUNK_DEPTH; ++z) {
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    for (int x = 0; x < CHUNK_WIDTH; ++x) {
                        size_t blockIndex = Chunk::localToIndex(x, y, z);
                        uint16_t blockID = currentChunkBlocks_ptr[blockIndex];

                        if (blockID == Blocks::AIR_ID) {
                            continue;
                        }

                        glm::ivec3 currentBlockLocalPos_ivec(x, y, z);
                        
                        bool cellFaceIsActuallyExposed[6] = {false}; // True if this cell face is open to air/non-occluding neighbor
                        bool anyCellFaceIsActuallyExposed = false;

                        // 3. Determine exposure status for all 6 conceptual cell faces
                        for (int faceIndex = 0; faceIndex < 6; ++faceIndex) { // Iterate 6 faces
                            glm::ivec3 neighborOffset = NEIGHBOR_OFFSETS[faceIndex];
                            glm::ivec3 neighborLocalPosRelativeToCurrentChunk = currentBlockLocalPos_ivec + neighborOffset;
                            uint16_t neighborBlockID_val;
                            bool neighborOccludesThisFace = true; // Assume occluded by default

                            // Check if neighbor is within the current chunk
                            if (neighborLocalPosRelativeToCurrentChunk.x >= 0 && neighborLocalPosRelativeToCurrentChunk.x < CHUNK_WIDTH &&
                                neighborLocalPosRelativeToCurrentChunk.y >= 0 && neighborLocalPosRelativeToCurrentChunk.y < CHUNK_HEIGHT &&
                                neighborLocalPosRelativeToCurrentChunk.z >= 0 && neighborLocalPosRelativeToCurrentChunk.z < CHUNK_DEPTH) {
                                
                                size_t neighborIdxInCurrentChunk = Chunk::localToIndex(
                                    neighborLocalPosRelativeToCurrentChunk.x,
                                    neighborLocalPosRelativeToCurrentChunk.y,
                                    neighborLocalPosRelativeToCurrentChunk.z
                                );
                                neighborBlockID_val = currentChunkBlocks_ptr[neighborIdxInCurrentChunk];
                                
                                if (neighborBlockID_val == Blocks::AIR_ID) {
                                    neighborOccludesThisFace = false;
                                } else {
                                    // Check if the solid neighbor has a full face pointing back at us
                                    neighborOccludesThisFace = blockRegistry.isBlockFaceFull(neighborBlockID_val, Block::getOppositeFace(static_cast<FaceDirection>(faceIndex)));
                                }
                            } else {
                                // Neighbor is in an adjacent chunk, use the cache
                                const CachedNeighborData& cachedNeighbor = neighborCache[faceIndex];

                                if (!cachedNeighbor.isGeneratedAndExists) {
                                    neighborOccludesThisFace = true; // Treat as occluded if neighbor chunk not ready/non-existent
                                } else {
                                    if (cachedNeighbor.isAllAir) {
                                        neighborBlockID_val = Blocks::AIR_ID;
                                        neighborOccludesThisFace = false;
                                    } else {
                                        // Neighbor chunk exists, is generated, and not all air. Get its block ID.
                                        // Calculate local coordinates within the *neighbor* chunk
                                        glm::ivec3 localPosInNeighborChunk(
                                            (currentBlockLocalPos_ivec.x + neighborOffset.x + CHUNK_WIDTH) % CHUNK_WIDTH,
                                            (currentBlockLocalPos_ivec.y + neighborOffset.y + CHUNK_HEIGHT) % CHUNK_HEIGHT,
                                            (currentBlockLocalPos_ivec.z + neighborOffset.z + CHUNK_DEPTH) % CHUNK_DEPTH
                                        );
                                        
                                        if (cachedNeighbor.blockData) { // Check if blockData has a value
                                            neighborBlockID_val = (*cachedNeighbor.blockData)[Chunk::localToIndex(
                                                localPosInNeighborChunk.x,
                                                localPosInNeighborChunk.y,
                                                localPosInNeighborChunk.z
                                            )];
                                            if (neighborBlockID_val == Blocks::AIR_ID) { 
                                                neighborOccludesThisFace = false;
                                            } else {
                                                neighborOccludesThisFace = blockRegistry.isBlockFaceFull(neighborBlockID_val, Block::getOppositeFace(static_cast<FaceDirection>(faceIndex)));
                                            }
                                        } else {
                                            // Should be caught by isAllAir, but as a fallback:
                                            neighborBlockID_val = Blocks::AIR_ID;
                                            neighborOccludesThisFace = false; // Effectively air
                                        }
                                    }
                                }
                            }

                            if (!neighborOccludesThisFace) {
                                cellFaceIsActuallyExposed[faceIndex] = true;
                                anyCellFaceIsActuallyExposed = true;
                            }
                        } // End face check loop

                        if (anyCellFaceIsActuallyExposed) {
                            std::shared_ptr<const SeparableModelData> separableModelDataPtr = resourceManager.getModelForBlockType(blockID);
                            const Block* blockDef = blockRegistry.getBlockDefinition(blockID); 

                            if (separableModelDataPtr && blockDef) { 
                                glm::vec3 currentBlockLocalPos_vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

                                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                                    if (cellFaceIsActuallyExposed[faceIdx]) { 
                                        const ModelData& faceGeom = separableModelDataPtr->canonicalFaces[faceIdx];
                                        addBlockModelToMeshData(meshData, faceGeom, currentBlockLocalPos_vec3);
                                    }
                                }
                                addBlockModelToMeshData(meshData, separableModelDataPtr->remainingGeometry, currentBlockLocalPos_vec3);
                            }
                        }
                    } // End x loop
                } // End y loop
            } // End z loop
        } // End lambda for currentChunk.performLockedRead
    ); // End call to currentChunk.performLockedRead

    return meshData;
}

} // namespace ChunkMesher