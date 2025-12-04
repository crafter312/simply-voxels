#include "World.hpp"
#include <cmath>  // For std::floor
#include <iostream> // For debugging output (can be removed later)
#include <algorithm> // For std::max
#include <mutex> // For std::unique_lock
#include <optional> // For std::optional
#include <limits>   // For std::numeric_limits
#include <shared_mutex> // For std::shared_lock and std::unique_lock
#include <random>   // For std::random_device, std::mt19937, std::uniform_real_distribution
#include <glm/gtc/constants.hpp> // For glm::pi()

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp> // For glm::to_string

//#define GLM_ENABLE_EXPERIMENTAL
#include "../Camera.hpp" // Include Camera definition
#include "../block/Blocks.hpp" // Include the centralized block definitions
#include "../resource/RegionManager.hpp" // Include RegionManager definition

World::World(std::shared_ptr<Camera> camera)
    : m_camera(camera), m_stopCompactionThread(false), m_initialChunkGenerationComplete(false) {
    m_regionManager = std::make_unique<WorldSave::RegionManager>("../run/regions/"); // Initialize RegionManager
    // Initially, enqueue chunks around the starting camera position (which is relative to the initial rebase origin 0,0,0)
    enqueueChunksNearCamera();

    // Start the compaction thread
    m_compactionThread = std::thread(&World::compactionThreadLoop, this);
    std::cout << "[World] Compaction thread started." << std::endl;
}

World::~World() {
    std::cout << "[World] Destructor started." << std::endl;
    // 1. Signal and join the compaction thread BEFORE saving chunks or destroying RegionManager
    if (m_compactionThread.joinable()) {
        std::cout << "[World] Stopping compaction thread..." << std::endl;
        m_stopCompactionThread.store(true);
        m_compactionThreadCv.notify_one(); // Notify the thread to wake up from its wait
        m_compactionThread.join();
        std::cout << "[World] Compaction thread joined." << std::endl;
    }

    // The std::unique_ptr m_regionManager will be automatically destroyed here.
    // Before it's destroyed, we should save any modified chunks.
    std::cout << "World destructor: Attempting to save modified chunks..." << std::endl;

    if (m_regionManager) {
        // A shared_lock is appropriate here because we are only iterating through m_chunks
        // and calling methods on the Chunk objects. We are not modifying the m_chunks map structure itself.
        // The saveChunkToFile method takes a Chunk&, implying it might modify the chunk (e.g., clear its dirty flag).
        // In a destructor, we assume other threads that might interact with these specific chunk objects
        // have been shut down or are no longer active.
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex);

        std::cout << "Iterating over " << m_chunks.size() << " loaded chunks to save modifications." << std::endl;
        int savedCount = 0;
        int isDirtyCount = 0;

        for (auto& pair : m_chunks) { // Use auto& to get Chunk& for pair.second
            std::shared_ptr<Chunk>& chunk_sptr = pair.second; // chunk_sptr is std::shared_ptr<Chunk>&
            if (chunk_sptr && chunk_sptr->isDirty()) { // Check if the chunk has unpersisted data changes
                isDirtyCount++;
                if (m_regionManager->saveChunkToFile(*chunk_sptr)) {
                    savedCount++;
                } else {
                    // Outputting coordinates directly to avoid needing glm::to_string and potential include issues.
                    std::cerr << "World Destructor: Warning: Failed to save chunk at coordinates ("
                              << pair.first.x << ", " << pair.first.y << ", " << pair.first.z
                              << ") during shutdown." << std::endl;
                }
            }
        }
        std::cout << "Found " << isDirtyCount << " chunks requiring save. Successfully saved " << savedCount << " chunks." << std::endl;
    } else {
        std::cout << "RegionManager is null, skipping chunk saving on shutdown." << std::endl;
    }
    std::cout << "[World] World destruction complete." << std::endl;
    // m_regionManager (and other members) will be destroyed automatically after this.
}

std::optional<glm::ivec3> World::worldToChunkCoordinates(glm::i64vec3 worldPosition) {
    // Perform floored division using integer arithmetic to avoid float precision issues.
    // CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH are positive.
    int64_t chunkX_i64 = worldPosition.x / CHUNK_SIDE_LENGTH;
    int64_t chunkY_i64 = worldPosition.y / CHUNK_SIDE_LENGTH;
    int64_t chunkZ_i64 = worldPosition.z / CHUNK_SIDE_LENGTH;

    // Adjust for negative coordinates if there's a non-zero remainder,
    // because integer division truncates towards zero.
    if (worldPosition.x < 0 && (worldPosition.x % CHUNK_SIDE_LENGTH != 0)) {
        chunkX_i64--;
    }
    if (worldPosition.y < 0 && (worldPosition.y % CHUNK_SIDE_LENGTH != 0)) {
        chunkY_i64--;
    }
    if (worldPosition.z < 0 && (worldPosition.z % CHUNK_SIDE_LENGTH != 0)) {
        chunkZ_i64--;
    }

    // Check if the 64-bit chunk coordinates fit into 32-bit integers
    if (chunkX_i64 < std::numeric_limits<int>::min() || chunkX_i64 > std::numeric_limits<int>::max() ||
        chunkY_i64 < std::numeric_limits<int>::min() || chunkY_i64 > std::numeric_limits<int>::max() ||
        chunkZ_i64 < std::numeric_limits<int>::min() || chunkZ_i64 > std::numeric_limits<int>::max()) {
        // One or more chunk coordinates are out of the 32-bit range
        return std::nullopt;
    }

    return glm::ivec3( // Safely cast to int and return
        static_cast<int>(chunkX_i64),
        static_cast<int>(chunkY_i64),
        static_cast<int>(chunkZ_i64)
    );

}

std::optional<glm::ivec3> World::worldToLocalCoordinates(glm::i64vec3 worldPosition, glm::ivec3 chunkCoord) {
    glm::i64vec3 localPos_i64;
    localPos_i64.x = worldPosition.x - static_cast<int64_t>(chunkCoord.x) * CHUNK_SIDE_LENGTH;
    localPos_i64.y = worldPosition.y - static_cast<int64_t>(chunkCoord.y) * CHUNK_SIDE_LENGTH;
    localPos_i64.z = worldPosition.z - static_cast<int64_t>(chunkCoord.z) * CHUNK_SIDE_LENGTH;

    // Check if the calculated local coordinates are within the valid range [0, CHUNK_DIMENSION - 1].
    // Mathematically, if chunkCoord was correctly derived from worldToChunkCoordinates(worldPosition),
    // these local coordinates should always be in range.
    // This check adds robustness if the function is called with inconsistent inputs,
    // or helps catch unexpected behavior.
    if (localPos_i64.x < 0 || localPos_i64.x >= CHUNK_SIDE_LENGTH ||
        localPos_i64.y < 0 || localPos_i64.y >= CHUNK_SIDE_LENGTH ||
        localPos_i64.z < 0 || localPos_i64.z >= CHUNK_SIDE_LENGTH) {
        // This indicates that the provided chunkCoord was not the canonical one for worldPosition,
        // or an unexpected calculation error occurred.
        // std::cerr << "Warning: worldToLocalCoordinates produced out-of-bounds local coordinates. "
        //           << "worldPos: (" << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z << "), "
        //           << "chunkCoord: (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "), "
        //           << "calculated local_i64: (" << localPos_i64.x << "," << localPos_i64.y << "," << localPos_i64.z << ")" << std::endl;
        return std::nullopt;
    }

    return glm::ivec3(
        static_cast<int>(localPos_i64.x),
        static_cast<int>(localPos_i64.y),
        static_cast<int>(localPos_i64.z)
    );
}

uint16_t World::getBlockID(glm::i64vec3 worldPosition) const {
    std::optional<glm::ivec3> optChunkCoord = worldToChunkCoordinates(worldPosition);
    if (!optChunkCoord) {
        // World position results in chunk coordinates outside the 32-bit integer range.
        return Blocks::AIR_ID;
    }
    glm::ivec3 chunkCoord = *optChunkCoord;

    { // Scope for shared_lock
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
        auto it = m_chunks.find(chunkCoord);
        if (it != m_chunks.end() && it->second) {
            const std::shared_ptr<Chunk>& chunk_sptr = it->second;
            if (!chunk_sptr->isGenerated()) {
                return Blocks::AIR_ID; // Chunk exists but not generated, treat as air
            }
            std::optional<glm::ivec3> optLocalPos = worldToLocalCoordinates(worldPosition, chunkCoord);
            if (!optLocalPos) {
                // This case should ideally not be reached if worldToChunkCoordinates is correct
                // and its result is used here. It implies a deeper logic error.
                std::cerr << "Critical Error: In getBlockID, worldToLocalCoordinates failed for worldPos ("
                          << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
                          << ") and chunkCoord ("
                          << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                          << "). This indicates an internal logic flaw." << std::endl;
                return Blocks::AIR_ID;
            }
            glm::ivec3 localPos = *optLocalPos;
            return chunk_sptr->getBlock(localPos.x, localPos.y, localPos.z);
        }
    }
    return Blocks::AIR_ID; // Chunk doesn't exist, so it's air. Using Blocks::AIR_ID for consistency.
}

void World::setBlockID(glm::i64vec3 worldPosition, uint16_t blockID) {
    std::optional<glm::ivec3> optChunkCoord = worldToChunkCoordinates(worldPosition);
    if (!optChunkCoord) {
        // World position results in chunk coordinates outside the 32-bit integer range.
        // Cannot set block here.
        // Optionally, log a warning:
        // std::cerr << "Warning: Attempted to set block at a position "
        //           << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
        //           << " which is outside the 32-bit chunk coordinate addressable range." << std::endl;
        return;
    }
    glm::ivec3 chunkCoord = *optChunkCoord;

    // getOrCreateChunk handles its own unique lock for potential modification of m_chunks
    std::shared_ptr<Chunk> chunk_sptr = getOrCreateChunk(chunkCoord);
    std::optional<glm::ivec3> optLocalPos = worldToLocalCoordinates(worldPosition, chunkCoord); // chunkCoord is correct here
    if (!optLocalPos) {
        // Similar to getBlockID, this indicates a critical internal logic error.
        std::cerr << "Critical Error: In setBlockID, worldToLocalCoordinates failed for worldPos ("
                  << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
                  << ") and chunkCoord ("
                  << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                  << "). Block will not be set. This indicates an internal logic flaw." << std::endl;
        return;
    }
    glm::ivec3 localPos = *optLocalPos;
    if (!chunk_sptr) { // Should not happen if getOrCreateChunk works
        std::cerr << "Critical Error: In setBlockID, getOrCreateChunk returned null for " << glm::to_string(chunkCoord) << std::endl;
        return;
    }
    chunk_sptr->setBlock(localPos.x, localPos.y, localPos.z, blockID); // This will call markModified on the chunk

    // If the modified block is on a boundary, mark the adjacent neighbor chunk(s) as dirty too.
    // This ensures the neighbor rebuilds its mesh considering the change.
    // The neighbor checks need a read lock.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for neighbor checks
        if (localPos.x == 0) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[1]; // -X
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        } else if (localPos.x == CHUNK_SIDE_LENGTH - 1) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[0]; // +X
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        }
        if (localPos.y == 0) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[3]; // -Y
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        } else if (localPos.y == CHUNK_SIDE_LENGTH - 1) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[2]; // +Y
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        }
        if (localPos.z == 0) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[5]; // -Z
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        } else if (localPos.z == CHUNK_SIDE_LENGTH - 1) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[4]; // +Z
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        }
    }
    m_changedChunks.insert(chunkCoord); // Mark this chunk as changed
}

std::shared_ptr<Chunk> World::getChunk(glm::ivec3 chunkCoord) {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        return it->second; // Returns std::shared_ptr<Chunk>
    }
    return nullptr; // Or std::shared_ptr<Chunk>() which is a null shared_ptr
}

std::shared_ptr<const Chunk> World::getChunk(glm::ivec3 chunkCoord) const {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        return it->second; // Returns std::shared_ptr<const Chunk> (implicitly convertible from std::shared_ptr<Chunk>)
    }
    return nullptr; // Or std::shared_ptr<const Chunk>()
}

std::shared_ptr<Chunk> World::getOrCreateChunk(glm::ivec3 chunkCoord) {
    std::unique_lock<std::shared_mutex> lock(m_chunks_mutex); // Write lock for potential emplace
    auto it = m_chunks.find(chunkCoord);
    if (it == m_chunks.end()) {
        // Chunk does not exist, create it and emplace it into the map
        // std::piecewise_construct allows constructing key and value in-place
        auto new_chunk_sptr = std::make_shared<Chunk>(chunkCoord);
        it = m_chunks.emplace(std::piecewise_construct, 
                              std::forward_as_tuple(chunkCoord),  // Arguments for glm::ivec3 key
                              std::forward_as_tuple(new_chunk_sptr)).first; // Arguments for std::shared_ptr<Chunk>
        m_changedChunks.insert(chunkCoord); // Mark newly created chunk as changed (needs a mesh)
    }
    return it->second; // Returns std::shared_ptr<Chunk>
}

bool World::isChunkLoaded(glm::ivec3 chunkCoord) const {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    return m_chunks.count(chunkCoord) > 0;
}

const std::map<glm::ivec3, std::shared_ptr<Chunk>, IVec3Comparator>& World::getChunkMap() const {
    // Note: Returning a reference to the map itself is tricky for thread safety
    // if the caller iterates it without holding a lock.
    // For now, assume callers (like VulkanRenderer::drawFrame) are on the main thread
    // or handle locking externally if this method is used by other threads.
    // A shared_lock here would only protect the immediate return, not subsequent use.
    return m_chunks;
}

void World::clearAllChunks() {
    std::cout << "[World] Clearing all chunks..." << std::endl;
    std::unique_lock<std::shared_mutex> lock(m_chunks_mutex); // Exclusive lock to modify data structures

    // 1. Save any modified chunks before clearing them
    if (m_regionManager) {
        int savedCount = 0;
        for (auto& pair : m_chunks) {
            if (pair.second && pair.second->isDirty()) {
                if (m_regionManager->saveChunkToFile(*pair.second)) {
                    savedCount++;
                }
            }
        }
        std::cout << "[World] Saved " << savedCount << " dirty chunks before clearing." << std::endl;
    }

    // 2. Move chunks to a deletion queue instead of clearing them directly.
    //    This avoids a long blocking call on the main thread as thousands of
    //    shared_ptr destructors are called. The compaction thread will handle deletion.
    {
        std::unique_lock<std::mutex> deletion_lock(m_chunkDeletionMutex);
        for (auto& pair : m_chunks) {
            m_chunkDeletionQueue.push(std::move(pair.second));
        }
    }
    m_chunks.clear();
    m_changedChunks.clear();

    // 3. Clear the loading/unloading queues
    std::queue<glm::ivec3> emptyLoadQueue;
    m_loadQueue.swap(emptyLoadQueue);

    std::queue<glm::ivec3> emptyUnloadQueue;
    m_unloadQueue.swap(emptyUnloadQueue);

    std::cout << "[World] All chunk data cleared." << std::endl;
}

void World::update(float deltaTime) {
    m_rebaseOccurredThisFrame = false; // Reset flag at the start of update
    checkAndRebase(); // Check and perform rebase first
    enqueueChunksNearCamera();
    enqueueChunksToUnload();
    processLoadQueue();
    processUnloadQueue();
}

void World::compactionThreadLoop() {
    std::cout << "[World] Compaction thread loop entered." << std::endl;
    std::unique_lock<std::mutex> lock(m_compactionThreadMutex); // Lock for condition variable

    while (!m_stopCompactionThread.load(std::memory_order_relaxed)) {
        // Unlock the mutex while processing, as processCompactionQueue might take time
        // and has its own internal locking.
        lock.unlock();

        // --- Process Chunk Deletion Queue ---
        // Also use this thread to asynchronously delete chunks.
        {
            std::unique_lock<std::mutex> deletion_lock(m_chunkDeletionMutex);
            // Process a few deletions per cycle to spread the load
            for (int i = 0; i < 100 && !m_chunkDeletionQueue.empty(); ++i) {
                // The shared_ptr is destroyed when it goes out of scope here,
                // which in turn calls the Chunk destructor.
                std::shared_ptr<Chunk> chunk_to_delete = m_chunkDeletionQueue.front();
                m_chunkDeletionQueue.pop();
            }
        }
        // --- End Chunk Deletion ---

        if (m_regionManager) {
            int processed_in_cycle = 0;
            // Try to process up to MAX_COMPACTIONS_PER_THREAD_CYCLE tasks from the queue
            while (processed_in_cycle < MAX_COMPACTIONS_PER_THREAD_CYCLE &&
                   !m_stopCompactionThread.load(std::memory_order_relaxed)) { // Re-check stop flag
                // processCompactionQueue processes one item if available and max_to_process > 0
                if (m_regionManager->processCompactionQueue(1)) {
                    processed_in_cycle++;
                } else {
                    break; // Queue is empty for now, or no task was processed
                }
            }
        }

        // Re-lock the mutex before waiting on the condition variable
        lock.lock();
        if (m_stopCompactionThread.load(std::memory_order_relaxed)) { // Check stop flag again before waiting
            break;
        }
        // Wait for the interval OR until notified (e.g., by the destructor)
        m_compactionThreadCv.wait_for(lock, COMPACTION_THREAD_INTERVAL, [this] {
            return m_stopCompactionThread.load(std::memory_order_relaxed);
        });
    }
    if(lock.owns_lock()) lock.unlock(); // Ensure lock is released if loop broke while holding it
    std::cout << "[World] Compaction thread loop exited." << std::endl;
}

void World::enqueueChunksNearCamera() {
    if (!m_camera) return;

    // Get camera's absolute chunk position
    glm::ivec3 cameraChunkPos = m_camera->getAbsoluteChunkPos();

    // Iterate over a cubic bounding box that encompasses the sphere
    glm::ivec3 minChunk = cameraChunkPos - glm::ivec3(LOAD_CHUNK_RADIUS);
    glm::ivec3 maxChunk = cameraChunkPos + glm::ivec3(LOAD_CHUNK_RADIUS);

    // The m_chunks.find() needs a read lock.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
        glm::ivec3 chunkCoord, diff;
        float distSq;
        for (int x = minChunk.x; x <= maxChunk.x; ++x) {
            for (int y = minChunk.y; y <= maxChunk.y; ++y) {
                for (int z = minChunk.z; z <= maxChunk.z; ++z) {
                    chunkCoord.x = x;
                    chunkCoord.y = y;
                    chunkCoord.z = z;

                    // Check Euclidean distance for spherical loading
                    diff = chunkCoord - cameraChunkPos;
                    distSq = static_cast<float>(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

                    if ((distSq <= LOAD_CHUNK_RADIUS_SQUARED) && (m_chunks.find(chunkCoord) == m_chunks.end()))
                        m_loadQueue.push(chunkCoord);
              }
          }
        }
    }
}

void World::enqueueChunksToUnload() {
    if (!m_camera) return;

    // Get camera's absolute chunk position
    glm::ivec3 cameraChunkPos = m_camera->getAbsoluteChunkPos();

    // Iterate through existing chunks and unload those outside the radius
    // Iterating m_chunks needs a read lock.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
        glm::ivec3 chunkCoord, diff;
        float distSq;
        for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
            chunkCoord = it->first;
            diff = chunkCoord - cameraChunkPos;
            distSq = static_cast<float>(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

            // If chunk is outside the spherical unload radius
            if (distSq > UNLOAD_CHUNK_RADIUS_SQUARED) m_unloadQueue.push(chunkCoord);
        }
    }
}


void World::processLoadQueue() {
    glm::ivec3 chunkCoord, neighborCoord;
    bool needsLoading, loadedFromFile;
    int loadedCount = 0;
    while (!m_loadQueue.empty() && (loadedCount < MAX_CHUNKS_TO_LOAD_PER_FRAME)) {
        chunkCoord = m_loadQueue.front();
        m_loadQueue.pop();

        // Check if the chunk is already loaded (shouldn't be, but double-check)
        {
            std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for find
            needsLoading = (m_chunks.find(chunkCoord) == m_chunks.end());
        }

        if (!needsLoading) continue; // skip if already loaded
        std::shared_ptr<Chunk> newChunk_sptr = getOrCreateChunk(chunkCoord); // getOrCreateChunk has own unique lock
        if (!newChunk_sptr) { // Should not happen
            std::cerr << "World::processLoadQueue - getOrCreateChunk returned null for " << glm::to_string(chunkCoord) << std::endl;
            continue;
        }

        // Attempt to load from file first
        loadedFromFile = m_regionManager->loadChunkFromFile(*newChunk_sptr);
        if (!loadedFromFile) newChunk_sptr->generate(); // if not loaded from file, generate it procedurally

        // Mark its 6 direct neighbors as dirty so they can update their meshes
        // relative to this newly generated and loaded chunk.
        {
            std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for neighbor checks
            for (size_t i = 0; i < NUM_NEIGHBORS; ++i) {
                neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
                if (!m_chunks.count(neighborCoord)) continue; // Skip if neighbor chunk doesn't exist
                m_changedChunks.insert(neighborCoord);
            }
        }

        loadedCount++; 
        // m_changedChunks is already updated by getOrCreateChunk if it's new
    }
    
    // After processing, if the load queue is empty and we haven't marked the initial generation as complete yet, do so now.
    if (m_loadQueue.empty() && !m_initialChunkGenerationComplete.load()) {
        m_initialChunkGenerationComplete.store(true);
        std::cout << "[World] Initial chunk generation complete." << std::endl;
    }
}

void World::processUnloadQueue() {
    while (!m_unloadQueue.empty()) {
        glm::ivec3 chunkCoord = m_unloadQueue.front();
        m_unloadQueue.pop();
        
        // Operations on m_chunks (saving, reading for neighbors, then erasing) need to be atomic
        // with respect to other accesses to m_chunks. A unique_lock is appropriate here.
        {
            std::unique_lock<std::shared_mutex> lock(m_chunks_mutex); // Write lock for erase and consistent neighbor check
            
            auto it = m_chunks.find(chunkCoord);
            if (it != m_chunks.end()) {
                std::shared_ptr<Chunk> chunk_to_unload_sptr = it->second;
                if (!chunk_to_unload_sptr) { // Should not happen if map entry exists
                    m_chunks.erase(it); // Clean up null entry
                    continue;
                }

                // Notify RegionManager that this chunk is being unloaded *before* saving or erasing
                m_regionManager->notifyChunkUnloaded(*chunk_to_unload_sptr);

                // Save the chunk if it needs saving before unloading
                if (chunk_to_unload_sptr->isDirty() && !m_regionManager->saveChunkToFile(*chunk_to_unload_sptr)) {
                    std::cerr << "Warning: Failed to save chunk " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << " during unload." << std::endl;
                }

                // Before erasing the chunk, mark its 6 direct neighbors as dirty.
                // This allows them to rebuild their meshes now that this chunk will be gone (effectively air).
                for (size_t i = 0; i < NUM_NEIGHBORS; ++i) {
                    glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
                    if (m_chunks.count(neighborCoord)) { // Check if the neighbor exists
                        m_changedChunks.insert(neighborCoord);
                    }
                }
                m_changedChunks.insert(chunkCoord); // Mark this chunk as changed (it's being removed)
                m_chunks.erase(it); // Erase using the iterator
            }
        }
    }
}

const std::set<glm::ivec3, IVec3Comparator>& World::getChangedChunks() const {
    return m_changedChunks;
}
void World::acknowledgeChunkChangeProcessed(const glm::ivec3& chunkCoord) {
    m_changedChunks.erase(chunkCoord);
}

void World::markAllChunksDirty() {
    // This function is now for general "all chunks need mesh update" scenarios,
    // not specifically for rebase model matrix updates.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for iterating m_chunks
        for (const auto& pair : m_chunks) {
            m_changedChunks.insert(pair.first); // Add to regular changed chunks for mesh processing
        }
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

World::ChunkGenStatus World::getChunkGeneratedStatus(glm::ivec3 chunkCoord) const {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex);
    auto it = m_chunks.find(chunkCoord);
    if (it == m_chunks.end()) {
        return ChunkGenStatus::NOT_FOUND;
    }
    const auto& chunk_sptr = it->second;
    // Chunk exists, check its generation status
    // The isGenerated() method on Chunk is atomic and safe to call here.
    if (!chunk_sptr || !chunk_sptr->isGenerated()) {
        return ChunkGenStatus::LOADED_NOT_GENERATED;
    }
    return ChunkGenStatus::LOADED_AND_GENERATED;
}

std::vector<PotentialCollisionBlock> World::getPotentialCollisionBlocks(
    const glm::ivec3& entityAbsoluteChunkPos,
    const glm::vec3& entityLocalPosInChunk, // Entity's feet, center XZ
    const glm::vec3& entityDimensions) const { // Width, Height, Depth

    std::vector<PotentialCollisionBlock> potentialBlocks;

    // Reserve a reasonable amount of space if entity dimensions are small,
    // e.g., for a player. This can avoid some reallocations.
    // Max blocks a small player might touch is around 2x3x2 blocks * 2x2x2 chunks = 12 * 8 = 96.
    potentialBlocks.reserve(100); // Adjust as needed, or base on entityDimensions

    // 1. Calculate entity's AABB relative to its own chunk's origin.
    //    entityLocalPosInChunk is at the entity's feet, centered on XZ.
    glm::vec3 entityAABBMinInOwnChunk(
        entityLocalPosInChunk.x - entityDimensions.x / 2.0f,
        entityLocalPosInChunk.y, // Feet
        entityLocalPosInChunk.z - entityDimensions.z / 2.0f
    );
    glm::vec3 entityAABBMaxInOwnChunk(
        entityLocalPosInChunk.x + entityDimensions.x / 2.0f,
        entityLocalPosInChunk.y + entityDimensions.y, // Top
        entityLocalPosInChunk.z + entityDimensions.z / 2.0f
    );

    // 2. Determine the range of chunk offsets this AABB spans, relative to entityAbsoluteChunkPos.
    //    These offsets tell us which neighboring (or same) chunks the AABB touches.
    glm::ivec3 minChunkOffset(
        static_cast<int>(std::floor(entityAABBMinInOwnChunk.x / CHUNK_SIDE_LENGTH)),
        static_cast<int>(std::floor(entityAABBMinInOwnChunk.y / CHUNK_SIDE_LENGTH)),
        static_cast<int>(std::floor(entityAABBMinInOwnChunk.z / CHUNK_SIDE_LENGTH))
    );
    glm::ivec3 maxChunkOffset(
        static_cast<int>(std::floor(entityAABBMaxInOwnChunk.x / CHUNK_SIDE_LENGTH)),
        static_cast<int>(std::floor(entityAABBMaxInOwnChunk.y / CHUNK_SIDE_LENGTH)),
        static_cast<int>(std::floor(entityAABBMaxInOwnChunk.z / CHUNK_SIDE_LENGTH))
    );

    // 3. Iterate through the absolute chunk coordinates overlapped by the entity's AABB.
    for (int cz_offset = minChunkOffset.z; cz_offset <= maxChunkOffset.z; ++cz_offset) {
        for (int cy_offset = minChunkOffset.y; cy_offset <= maxChunkOffset.y; ++cy_offset) {
            for (int cx_offset = minChunkOffset.x; cx_offset <= maxChunkOffset.x; ++cx_offset) {
                glm::ivec3 currentRelativeChunkOffset(cx_offset, cy_offset, cz_offset);
                glm::ivec3 iterChunkAbsCoord = entityAbsoluteChunkPos + currentRelativeChunkOffset;

                std::shared_ptr<const Chunk> chunk_sptr = getChunk(iterChunkAbsCoord); // Read-only access
                if (!chunk_sptr || !chunk_sptr->isGenerated()) { // Skip if chunk isn't loaded or generated
                    continue;
                }

                // Pre-calculate the world origin of the current iterated chunk
                glm::i64vec3 iterChunkWorldOrigin(
                    static_cast<int64_t>(iterChunkAbsCoord.x) * CHUNK_SIDE_LENGTH,
                    static_cast<int64_t>(iterChunkAbsCoord.y) * CHUNK_SIDE_LENGTH,
                    static_cast<int64_t>(iterChunkAbsCoord.z) * CHUNK_SIDE_LENGTH
                );

                // 4. Transform entity's AABB into the local coordinate system of iterChunkAbsCoord.
                //    The offset (in local units) from entity's chunk origin to iterChunk's origin is:
                //    (entityAbsoluteChunkPos - iterChunkAbsCoord) * CHUNK_DIMENSIONS
                //    which simplifies to -currentRelativeChunkOffset * CHUNK_DIMENSIONS.
                glm::vec3 offsetToIterChunkOriginInLocalUnits(
                    static_cast<float>(-currentRelativeChunkOffset.x * CHUNK_SIDE_LENGTH),
                    static_cast<float>(-currentRelativeChunkOffset.y * CHUNK_SIDE_LENGTH),
                    static_cast<float>(-currentRelativeChunkOffset.z * CHUNK_SIDE_LENGTH)
                );

                glm::vec3 entityAABBMinInIterChunk = entityAABBMinInOwnChunk + offsetToIterChunkOriginInLocalUnits;
                glm::vec3 entityAABBMaxInIterChunk = entityAABBMaxInOwnChunk + offsetToIterChunkOriginInLocalUnits;

                // 5. Determine block indices within iterChunkAbsCoord covered by this transformed AABB.
                glm::ivec3 startBlockLocal(
                    static_cast<int>(std::floor(entityAABBMinInIterChunk.x)),
                    static_cast<int>(std::floor(entityAABBMinInIterChunk.y)),
                    static_cast<int>(std::floor(entityAABBMinInIterChunk.z))
                );
                glm::ivec3 endBlockLocal( // Inclusive end block index
                    static_cast<int>(std::floor(entityAABBMaxInIterChunk.x)),
                    static_cast<int>(std::floor(entityAABBMaxInIterChunk.y)),
                    static_cast<int>(std::floor(entityAABBMaxInIterChunk.z))
                );

                // 6. Clamp block indices to be within the current chunk's bounds [0, CHUNK_DIM-1].
                glm::ivec3 clampedStartBlockLocal(std::max(0, startBlockLocal.x), std::max(0, startBlockLocal.y), std::max(0, startBlockLocal.z));
                glm::ivec3 clampedEndBlockLocal(std::min(CHUNK_SIDE_LENGTH - 1, endBlockLocal.x), std::min(CHUNK_SIDE_LENGTH - 1, endBlockLocal.y), std::min(CHUNK_SIDE_LENGTH - 1, endBlockLocal.z));

                // 7. Iterate over the blocks in iterChunkAbsCoord.
                for (int ly = clampedStartBlockLocal.y; ly <= clampedEndBlockLocal.y; ++ly) {
                    for (int lz = clampedStartBlockLocal.z; lz <= clampedEndBlockLocal.z; ++lz) {
                        for (int lx = clampedStartBlockLocal.x; lx <= clampedEndBlockLocal.x; ++lx) {                            
                            uint16_t blockID = chunk_sptr->getBlock(lx, ly, lz); // Direct access since we have the chunk
                            if (blockID != Blocks::AIR_ID) {
                                potentialBlocks.push_back({iterChunkWorldOrigin + glm::i64vec3(lx, ly, lz), blockID});
                            }
                        }
                    }
                }
            }
        }
    }
    return potentialBlocks;
}

std::optional<glm::i64vec3> World::getPlayerSpawnPos() const {
    std::random_device rd;
    std::mt19937 gen(rd());

    // 1. Select a random XZ chunk coordinate within SPAWN_CHUNK_RADIUS around (0,0)
    std::uniform_real_distribution<> angle_dist(0.0, 2.0 * glm::pi<double>());
    std::uniform_real_distribution<> radius_dist(0.0, static_cast<double>(SPAWN_CHUNK_RADIUS));

    double angle = angle_dist(gen);
    double radius_in_chunks = radius_dist(gen);

    int spawnChunkX = static_cast<int>(std::round(radius_in_chunks * std::cos(angle)));
    int spawnChunkZ = static_cast<int>(std::round(radius_in_chunks * std::sin(angle)));

    // Check if the chosen chunk column (at a representative Y level, e.g., Y=0) is generated.
    // Terrain generation is primarily XZ dependent, so checking one chunk in the column is usually sufficient.
    // If your world has distinct generation patterns at different Y chunk levels, you might need a more sophisticated check.
    glm::ivec3 representativeChunkCoordForGenCheck(spawnChunkX, 0, spawnChunkZ); // Using Y=0 as representative
    if (getChunkGeneratedStatus(representativeChunkCoordForGenCheck) != ChunkGenStatus::LOADED_AND_GENERATED) {
        return std::nullopt; // Chosen spawn chunk area is not yet generated
    }

    // 2. Select a random XZ local block coordinate within that chunk
    std::uniform_int_distribution<> local_block_dist(0, CHUNK_SIDE_LENGTH - 1); // Assuming CHUNK_WIDTH == CHUNK_DEPTH
    int localBlockX = local_block_dist(gen);
    int localBlockZ = local_block_dist(gen);

    // 3. Calculate absolute world XZ coordinates
    int64_t worldSpawnX = static_cast<int64_t>(spawnChunkX) * CHUNK_SIDE_LENGTH + localBlockX;
    int64_t worldSpawnZ = static_cast<int64_t>(spawnChunkZ) * CHUNK_SIDE_LENGTH + localBlockZ;

    // 4. Scan downwards from a maximum height to find the highest non-air block
    //    Adjust MAX_WORLD_Y_SEARCH_SPAWN if your world can be taller.
    //    This assumes a typical Minecraft-like height range.
    const int64_t MAX_WORLD_Y_SEARCH_SPAWN = CHUNK_SIDE_LENGTH * 8; // e.g., Y=128 if CHUNK_HEIGHT=16
    const int64_t MIN_WORLD_Y_SEARCH_SPAWN = 0;               // Don't spawn below Y=0

    for (int64_t currentY = MAX_WORLD_Y_SEARCH_SPAWN; currentY >= MIN_WORLD_Y_SEARCH_SPAWN; --currentY) {
        uint16_t blockID = getBlockID({worldSpawnX, currentY, worldSpawnZ});
        if (blockID != Blocks::AIR_ID) {
            // Found the highest solid block at currentY. Spawn player one block above it.
            // Ensure the spot directly above is also air for basic safety (2-block high space).
            // uint16_t blockAboveID = getBlockID({worldSpawnX, currentY + 1, worldSpawnZ});
            // uint16_t blockTwoAboveID = getBlockID({worldSpawnX, currentY + 2, worldSpawnZ});
            // if (blockAboveID == Blocks::AIR_ID && blockTwoAboveID == Blocks::AIR_ID) { // Add this check for robustness
            return glm::i64vec3(worldSpawnX, currentY + 1, worldSpawnZ);
            // }
        }
    }

    // Fallback: If no suitable ground found (e.g., over a void), return nullopt.
    return std::nullopt;
}

bool World::isInitialChunkGenerationComplete() const {
    return m_initialChunkGenerationComplete.load();
}