#include "World.hpp"
#include <random> // For random number generation
#include <cmath>  // For std::floor
#include "../Camera.hpp" // Include Camera definition

// Define a default solid block ID for random generation (e.g., "dirt")
const uint16_t DIRT_BLOCK_ID = 1; // Assuming 1 is a valid block ID like "dirt"

World::World(std::shared_ptr<Camera> camera)
    : m_camera(camera) {
    // Initialize a random number generator
    std::random_device rd;  // Obtain a random number from hardware
    std::mt19937 gen(rd()); // Seed the generator

    // Define the range for coordinates (e.g., 0 to 31 for x, z and 0 to 1 for y for a flatter world)
    std::uniform_int_distribution<> xz_distrib(0, 31);
    std::uniform_int_distribution<> y_distrib(0, 1); // Keep it low for initial testing

    // Define the number of blocks to generate
    int num_blocks_to_create = 200; // Generate more blocks to see chunking in action

    // Generate random blocks
    for (int i = 0; i < num_blocks_to_create; ++i) {
        glm::ivec3 blockPos(
            xz_distrib(gen),
            y_distrib(gen),
            xz_distrib(gen)
        );
        setBlockID(blockPos, DIRT_BLOCK_ID);
    }

    if (!m_camera) {
        // Handle the case where a null camera is passed, if necessary
        // For example, throw an exception or log a warning.
    }
}

glm::ivec3 World::worldToChunkCoordinates(glm::ivec3 worldPosition) {
    return glm::ivec3(
        static_cast<int>(std::floor(static_cast<float>(worldPosition.x) / CHUNK_WIDTH)),
        static_cast<int>(std::floor(static_cast<float>(worldPosition.y) / CHUNK_HEIGHT)),
        static_cast<int>(std::floor(static_cast<float>(worldPosition.z) / CHUNK_DEPTH))
    );
}

glm::ivec3 World::worldToLocalCoordinates(glm::ivec3 worldPosition, glm::ivec3 chunkCoord) {
    return glm::ivec3(
        worldPosition.x - chunkCoord.x * CHUNK_WIDTH,
        worldPosition.y - chunkCoord.y * CHUNK_HEIGHT,
        worldPosition.z - chunkCoord.z * CHUNK_DEPTH
    );
}

uint16_t World::getBlockID(glm::ivec3 worldPosition) const {
    glm::ivec3 chunkCoord = worldToChunkCoordinates(worldPosition);
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        const Chunk& chunk = it->second;
        glm::ivec3 localPos = worldToLocalCoordinates(worldPosition, chunkCoord);
        return chunk.getBlock(localPos.x, localPos.y, localPos.z);
    }
    return AIR_BLOCK_ID; // Chunk doesn't exist, so it's air
}

void World::setBlockID(glm::ivec3 worldPosition, uint16_t blockID) {
    glm::ivec3 chunkCoord = worldToChunkCoordinates(worldPosition);
    Chunk& chunk = getOrCreateChunk(chunkCoord);
    glm::ivec3 localPos = worldToLocalCoordinates(worldPosition, chunkCoord);
    chunk.setBlock(localPos.x, localPos.y, localPos.z, blockID);
}

Chunk* World::getChunk(glm::ivec3 chunkCoord) {
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

const Chunk* World::getChunk(glm::ivec3 chunkCoord) const {
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

Chunk& World::getOrCreateChunk(glm::ivec3 chunkCoord) {
    auto it = m_chunks.find(chunkCoord);
    if (it == m_chunks.end()) {
        // Chunk does not exist, create it and emplace it into the map
        // std::piecewise_construct allows constructing key and value in-place
        it = m_chunks.emplace(std::piecewise_construct,
                              std::forward_as_tuple(chunkCoord),  // Arguments for glm::ivec3 key
                              std::forward_as_tuple(chunkCoord)).first; // Arguments for Chunk constructor
    }
    return it->second;
}

const std::map<glm::ivec3, Chunk, IVec3Comparator>& World::getChunkMap() const {
    return m_chunks;
}