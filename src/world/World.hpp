#ifndef WORLD_HPP
#define WORLD_HPP

#include <map>
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp> // For glm::ivec3
#include <memory> // For std::shared_ptr
#include <optional> // For std::optional
#include <set>    // To track changed chunks efficiently
#include <queue> // For chunk loading/unloading queue
#include <thread> // For std::thread
#include <atomic> // For std::atomic<bool>
#include <chrono> // For std::chrono
#include <shared_mutex> // For std::shared_mutex
#include <future> // For std::future
#include <condition_variable> // For std::condition_variable
#include "Chunk.hpp" // Include the Chunk definition

// Forward declaration for Camera
class Camera;

namespace WorldSave { // Forward declare RegionManager in its namespace
struct LoadResult; // Forward declaration for LoadResult
class RegionManager;
}

// Comparator for glm::ivec3 to use it as a key in std::map
struct IVec3Comparator {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};

// Define neighbor offsets as a constant array
constexpr glm::ivec3 NEIGHBOR_OFFSETS[] = {
    {1, 0, 0}, {-1, 0, 0},
    {0, 1, 0}, {0, -1, 0},
    {0, 0, 1}, {0, 0, -1}
};
constexpr size_t NUM_NEIGHBORS = sizeof(NEIGHBOR_OFFSETS) / sizeof(NEIGHBOR_OFFSETS[0]);

// Struct to hold information about a block potentially colliding with an entity
struct PotentialCollisionBlock {
    glm::i64vec3 worldPosition; // Absolute world coordinate of the block's origin
    uint16_t blockID;
};

// --- Constants for chunk management ---
static constexpr int LOAD_CHUNK_RADIUS = 8; // Radius in chunks around the camera to load chunks
static constexpr int SPAWN_CHUNK_RADIUS = 1;  // Max radius in chunks from origin (0,0) to pick a spawn chunk XZ
static constexpr int REBASE_TRIGGER_RADIUS_CHUNKS = 2048; // Radius in chunks from rebase origin to trigger a rebase (remains large)
static constexpr int UNLOAD_CHUNK_RADIUS = LOAD_CHUNK_RADIUS + 2; // Increased: Radius in chunks beyond which to unload chunks (was 3)
static constexpr int MAX_CHUNKS_TO_LOAD_PER_FRAME = 4; // Increased: Max chunks to process from load queue per frame (was 2)

// --- Constants for Compaction Thread ---
static constexpr std::chrono::seconds COMPACTION_THREAD_INTERVAL(300); // Check every 5 minutes (300 seconds)
static constexpr int MAX_COMPACTIONS_PER_THREAD_CYCLE = 5; // Max regions to compact in one go by the thread

static constexpr float LOAD_CHUNK_RADIUS_SQUARED = static_cast<float>(LOAD_CHUNK_RADIUS * LOAD_CHUNK_RADIUS);
static constexpr float UNLOAD_CHUNK_RADIUS_SQUARED = static_cast<float>(UNLOAD_CHUNK_RADIUS * UNLOAD_CHUNK_RADIUS);

// Class to manage all blocks in the world
class World {
public:
    ~World(); // Declare the destructor
    World(std::shared_ptr<Camera> camera);

    // Destructor (optional, but good practice)
    // Gets the block ID at the given world position.
    // Returns Blocks::AIR_ID if the chunk doesn't exist or the block is air.
    uint16_t getBlockID(glm::i64vec3 worldPosition) const;

    // Sets the block ID at the given world position.
    // This may involve creating a new chunk if one doesn't exist at that location.
    void setBlockID(glm::i64vec3 worldPosition, uint16_t blockID);

    // Retrieves a pointer to a chunk at the given chunk coordinates.
    // Returns nullptr if the chunk does not exist.
    std::shared_ptr<Chunk> getChunk(glm::ivec3 chunkCoord);
    std::shared_ptr<const Chunk> getChunk(glm::ivec3 chunkCoord) const;

    // Retrieves a reference to a chunk at the given chunk coordinates.
    // If the chunk does not exist, it is created.
    std::shared_ptr<Chunk> getOrCreateChunk(glm::ivec3 chunkCoord);
    bool isChunkLoaded(glm::ivec3 chunkCoord) const; // New method

    // Helper to convert world coordinates to chunk coordinates
    static std::optional<glm::ivec3> worldToChunkCoordinates(glm::i64vec3 worldPosition);
    // Helper to convert world coordinates to local block coordinates within a chunk
    static std::optional<glm::ivec3> worldToLocalCoordinates(glm::i64vec3 worldPosition, glm::ivec3 chunkCoord);

    // Provides access to the underlying chunk map for iteration (e.g., by the renderer)
    const std::map<glm::ivec3, std::shared_ptr<Chunk>, IVec3Comparator>& getChunkMap() const;

    // Call this periodically to process loading/unloading
    void update(float deltaTime);

    // Get the current rebase origin in chunk coordinates
    glm::ivec3 getRebaseOriginChunkCoord() const { return m_rebaseOriginChunkCoord; }

    const std::set<glm::ivec3, IVec3Comparator>& getChangedChunks() const; // IVec3Comparator for std::set
    void acknowledgeChunkChangeProcessed(const glm::ivec3& chunkCoord); // New method
    void markAllChunksDirty(); // Mark all loaded chunks for mesh regeneration (e.g., texture change)
    bool rebaseOccurredLastFrame() const; // Check if a rebase happened

    // Retrieves a list of blocks (and their IDs) that potentially intersect with an AABB
    // defined by the entity's chunk position, local position, and dimensions.
    std::vector<PotentialCollisionBlock> getPotentialCollisionBlocks(
        const glm::ivec3& entityAbsoluteChunkPos,
        const glm::vec3& entityLocalPosInChunk, // Player's feet, center XZ
        const glm::vec3& entityDimensions) const;

    // Determines a suitable spawn position for the player.
    std::optional<glm::i64vec3> getPlayerSpawnPos() const;

private:
    std::map<glm::ivec3, std::shared_ptr<Chunk>, IVec3Comparator> m_chunks;
    std::shared_ptr<Camera> m_camera; // Store a pointer to the camera    
    std::set<glm::ivec3, IVec3Comparator> m_changedChunks; // Set of chunk coordinates that have been modified

    glm::ivec3 m_rebaseOriginChunkCoord = glm::ivec3(0,0,0); // The current origin for rendering
    bool m_rebaseOccurredThisFrame = false; // Flag to indicate a rebase happened in the current update
    // Queues for loading and unloading chunks.  They hold chunk coordinates.
    std::queue<glm::ivec3> m_loadQueue;
    std::queue<glm::ivec3> m_unloadQueue;
    mutable std::shared_mutex m_chunks_mutex; // Mutex to protect m_chunks
    std::unique_ptr<WorldSave::RegionManager> m_regionManager; // Instance of RegionManager, using unique_ptr

    // Background compaction thread
    // For asynchronous chunk loading
    std::vector<std::pair<glm::ivec3, std::future<WorldSave::LoadResult>>> m_pendingLoadFutures;
    mutable std::mutex m_pendingLoadFuturesMutex; // Mutex to protect m_pendingLoadFutures

    std::thread m_compactionThread;
    std::atomic<bool> m_stopCompactionThread;
    std::mutex m_compactionThreadMutex; // Mutex for the condition variable
    std::condition_variable m_compactionThreadCv; // Condition variable to wake the thread
    void compactionThreadLoop();

    // Internal methods for managing chunk loading/unloading
    void enqueueChunksNearCamera();
    void enqueueChunksToUnload();
    void processLoadQueue();
    void processCompletedLoads(); // New method to handle finished async loads
    void processUnloadQueue();
    void checkAndRebase(); // Check if rebase is needed and perform it
};

#endif // WORLD_HPP