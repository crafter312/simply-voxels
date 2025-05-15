#include "World.hpp"
#include <random> // For random number generation
#include <cmath>  // For std::floor
#include <iostream> // For debugging output (can be removed later)
#include <algorithm> // For std::max

//#define GLM_ENABLE_EXPERIMENTAL
//#include <glm/gtx/string_cast.hpp> // For glm::to_string
#include "../Camera.hpp" // Include Camera definition
#include "../block/Blocks.hpp" // Include the centralized block definitions

World::World(std::shared_ptr<Camera> camera)
    : m_camera(camera) {
    // Initialize a random number generator
    std::random_device rd;  // Obtain a random number from hardware
    std::mt19937 gen(rd()); // Seed the generator
    std::uniform_int_distribution<> block_type_distrib(0, 1); // For choosing between dirt and stone
    
    // Initially, enqueue chunks around the starting camera position (which is relative to the initial rebase origin 0,0,0)
    enqueueChunksNearCamera();
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
    return Blocks::AIR_ID; // Chunk doesn't exist, so it's air. Using Blocks::AIR_ID for consistency.
}

void World::setBlockID(glm::ivec3 worldPosition, uint16_t blockID) {
    glm::ivec3 chunkCoord = worldToChunkCoordinates(worldPosition);
    Chunk& chunk = getOrCreateChunk(chunkCoord);
    glm::ivec3 localPos = worldToLocalCoordinates(worldPosition, chunkCoord);
    chunk.setBlock(localPos.x, localPos.y, localPos.z, blockID); // This will call markModified on the chunk
    m_changedChunks.insert(chunkCoord); // Mark this chunk as changed
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
        m_changedChunks.insert(chunkCoord); // Mark newly created chunk as changed (needs a mesh)
    }
    return it->second;
}

const std::map<glm::ivec3, Chunk, IVec3Comparator>& World::getChunkMap() const {
    return m_chunks;
}

void World::update(float deltaTime) {
    m_rebaseOccurredThisFrame = false; // Reset flag at the start of update
    checkAndRebase(); // Check and perform rebase first
    enqueueChunksNearCamera();
    enqueueChunksToUnload();
    processLoadQueue();
    processUnloadQueue();
}

void World::enqueueChunksNearCamera() {
    if (!m_camera) return;

    // Get camera's absolute chunk position
    glm::ivec3 cameraChunkPos = m_camera->getAbsoluteChunkPos();

    // Calculate the bounds of the cubic region in chunk coordinates
    glm::ivec3 minChunk = cameraChunkPos - glm::ivec3(LOAD_CHUNK_RADIUS);
    glm::ivec3 maxChunk = cameraChunkPos + glm::ivec3(LOAD_CHUNK_RADIUS);

    // Iterate through all chunks in the cubic region using a single loop
    for (int x = minChunk.x; x <= maxChunk.x; ++x) {
        for (int y = minChunk.y; y <= maxChunk.y; ++y) {
            for (int z = minChunk.z; z <= maxChunk.z; ++z) {
                glm::ivec3 chunkCoord(x, y, z);
                if (m_chunks.find(chunkCoord) == m_chunks.end()) m_loadQueue.push(chunkCoord);
          }
        }
    }
}

void World::enqueueChunksToUnload() {
    if (!m_camera) return;

    // Get camera's absolute chunk position
    glm::ivec3 cameraChunkPos = m_camera->getAbsoluteChunkPos();

    // Iterate through existing chunks and unload those outside the radius
    for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
        glm::ivec3 chunkCoord = it->first;

        // Calculate distance in chunk coordinates (Chebyshev distance for a cubic region)
        int dx = std::abs(chunkCoord.x - cameraChunkPos.x);
        int dy = std::abs(chunkCoord.y - cameraChunkPos.y);
        int dz = std::abs(chunkCoord.z - cameraChunkPos.z);

        if (dx > UNLOAD_CHUNK_RADIUS || dy > UNLOAD_CHUNK_RADIUS || dz > UNLOAD_CHUNK_RADIUS) {
            m_unloadQueue.push(chunkCoord);
            // std::cout << "Enqueueing chunk to unload: " << glm::to_string(chunkCoord) << std::endl;
        }
    }
}

void World::processLoadQueue() {
    int loadedCount = 0;

    while (!m_loadQueue.empty() && loadedCount < MAX_CHUNKS_TO_LOAD_PER_FRAME) {
        glm::ivec3 chunkCoord = m_loadQueue.front();
        m_loadQueue.pop();

        // Check if the chunk is already loaded (shouldn't be, but double-check)
        if (m_chunks.find(chunkCoord) == m_chunks.end()) {
            // Create and load the chunk data here (replace with actual loading logic)
            // For now, simulate with random blocks like in the constructor, but in the new chunk
            Chunk& newChunk = getOrCreateChunk(chunkCoord);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> block_type_distrib(0, 1);
            std::uniform_int_distribution<> x_distrib(0, CHUNK_WIDTH - 1);
            std::uniform_int_distribution<> y_distrib(0, CHUNK_HEIGHT - 1);
            std::uniform_int_distribution<> z_distrib(0, CHUNK_DEPTH - 1);
            for (int i = 0; i < 100; ++i) { // Generate some random blocks
                glm::ivec3 blockPos(x_distrib(gen), y_distrib(gen), z_distrib(gen));
                uint16_t blockID = (block_type_distrib(gen) == 0) ? Blocks::DIRT_ID : Blocks::STONE_ID;
                newChunk.setBlock(blockPos.x, blockPos.y, blockPos.z, blockID);
            }
            // std::cout << "Loaded chunk: " << glm::to_string(chunkCoord) << std::endl;
            loadedCount++; 
            // m_changedChunks is already updated by getOrCreateChunk if it's new
        }
    }
}

void World::processUnloadQueue() {
    while (!m_unloadQueue.empty()) {
        glm::ivec3 chunkCoord = m_unloadQueue.front();
        m_unloadQueue.pop();
        // std::cout << "Unloading chunk: " << glm::to_string(chunkCoord) << std::endl;
        m_changedChunks.insert(chunkCoord); // Mark this chunk as changed (it's being removed)
        m_chunks.erase(chunkCoord);
    }
}

const std::set<glm::ivec3, IVec3Comparator>& World::getChangedChunks() const {
    return m_changedChunks;
}
void World::clearChangedChunks() {
    m_changedChunks.clear();
}

void World::markAllChunksDirty() {
    // This function is now for general "all chunks need mesh update" scenarios,
    // not specifically for rebase model matrix updates.
    for (const auto& pair : m_chunks) {
        m_changedChunks.insert(pair.first); // Add to regular changed chunks for mesh processing
    }
    std::cout << "Marked all " << m_chunks.size() << " chunks as dirty." << std::endl;
}

void World::checkAndRebase() {
    if (!m_camera) return;

    // Get camera's absolute chunk position directly
    glm::ivec3 cameraCurrentChunkPos = m_camera->getAbsoluteChunkPos();

    // Calculate distance from the current rebase origin chunk
    glm::ivec3 diff = cameraCurrentChunkPos - m_rebaseOriginChunkCoord;
    int max_dist = std::max({std::abs(diff.x), std::abs(diff.y), std::abs(diff.z)});

    if (max_dist > REBASE_TRIGGER_RADIUS_CHUNKS) {
        glm::ivec3 oldRebaseOriginChunkCoord = m_rebaseOriginChunkCoord;
        m_rebaseOriginChunkCoord = cameraCurrentChunkPos; // New origin is the chunk the camera is in
        m_rebaseOccurredThisFrame = true; // Set the flag
        // std::cout << "Rebasing origin from " << glm::to_string(oldRebaseOriginChunkCoord) << " to " << glm::to_string(m_rebaseOriginChunkCoord) << std::endl;
        // No longer calling markAllChunksDirty() here for rebase.
        // The renderer will detect m_rebaseOccurredThisFrame.
    }
}

bool World::rebaseOccurredLastFrame() const {
    return m_rebaseOccurredThisFrame;
}