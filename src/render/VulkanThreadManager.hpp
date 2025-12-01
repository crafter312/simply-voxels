#ifndef VULKAN_THREAD_MANAGER_HPP
#define VULKAN_THREAD_MANAGER_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <optional>
#include <glm/vec3.hpp>
#include "../resource/ModelLoader.hpp" // For ModelData
#include "../world/World.hpp"         // For IVec3Comparator

// Forward declarations
class World;
class ResourceManager;
class BlockRegistry;

/**
 * @class VulkanThreadManager
 * @brief Manages background threads for expensive, non-rendering tasks.
 *
 * Currently, this class manages a single dedicated worker thread for chunk meshing.
 * It uses a producer-consumer pattern with thread-safe queues to submit jobs
 * and retrieve results.
 */
class VulkanThreadManager {
public:
    struct MeshResult {
        glm::ivec3 chunkCoord;
        ModelData meshData;
    };

    VulkanThreadManager(World& world, ResourceManager& resourceManager, BlockRegistry& blockRegistry);
    ~VulkanThreadManager();

    // Disable copy and move to prevent issues with thread and mutex ownership.
    VulkanThreadManager(const VulkanThreadManager&) = delete;
    VulkanThreadManager& operator=(const VulkanThreadManager&) = delete;
    VulkanThreadManager(VulkanThreadManager&&) = delete;
    VulkanThreadManager& operator=(VulkanThreadManager&&) = delete;

    void start();
    void stop();

    void submitMeshJob(const glm::ivec3& chunkCoord);
    std::optional<MeshResult> getMeshResult();
    bool hasMeshResults();
    bool isIdle() const;

private:
    void mesherThreadLoop();

    // References to engine systems needed by the worker thread
    World& m_world;
    ResourceManager& m_resourceManager;
    BlockRegistry& m_blockRegistry;

    // --- Mesher Thread Members ---
    std::thread m_mesherThread;
    std::atomic<bool> m_stopMesherThread{false};
    std::atomic<bool> m_isMeshing{false}; // Tracks if a job is currently being processed

    std::queue<glm::ivec3> m_meshWorkQueue;
    mutable std::mutex m_workQueueMutex;
    std::condition_variable m_workQueueCV;

    std::queue<MeshResult> m_meshResultsQueue;
    std::mutex m_resultsQueueMutex;
};

#endif // VULKAN_THREAD_MANAGER_HPP