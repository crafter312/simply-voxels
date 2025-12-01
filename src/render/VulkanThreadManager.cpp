#include "VulkanThreadManager.hpp"
#include "../world/World.hpp"
#include "../resource/ResourceManager.hpp"
#include "../world/ChunkMesher.hpp"
#include <iostream>

VulkanThreadManager::VulkanThreadManager(World& world, ResourceManager& resourceManager, BlockRegistry& blockRegistry)
    : m_world(world),
      m_resourceManager(resourceManager),
      m_blockRegistry(blockRegistry),
      m_isMeshing(false) { // Initialize the flag
    std::cout << "VulkanThreadManager constructed." << std::endl;
}

VulkanThreadManager::~VulkanThreadManager() {
    stop(); // Ensure thread is stopped on destruction
    std::cout << "VulkanThreadManager destructed." << std::endl;
}

void VulkanThreadManager::start() {
    if (m_mesherThread.joinable()) {
        std::cout << "VulkanThreadManager: Mesher thread already running." << std::endl;
        return;
    }
    std::cout << "VulkanThreadManager: Starting mesher thread..." << std::endl;
    m_stopMesherThread.store(false);
    m_mesherThread = std::thread(&VulkanThreadManager::mesherThreadLoop, this);
}

void VulkanThreadManager::stop() {
    if (m_mesherThread.joinable()) {
        std::cout << "VulkanThreadManager: Stopping mesher thread..." << std::endl;
        m_stopMesherThread.store(true);
        m_workQueueCV.notify_one(); // Wake up the thread if it's waiting
        m_mesherThread.join();
        std::cout << "VulkanThreadManager: Mesher thread joined." << std::endl;
    }
}

void VulkanThreadManager::submitMeshJob(const glm::ivec3& chunkCoord) {
    {
        std::unique_lock<std::mutex> lock(m_workQueueMutex);
        m_meshWorkQueue.push(chunkCoord);
    }
    m_workQueueCV.notify_one(); // Notify the worker thread that a job is available
}

std::optional<VulkanThreadManager::MeshResult> VulkanThreadManager::getMeshResult() {
    std::unique_lock<std::mutex> lock(m_resultsQueueMutex);
    if (m_meshResultsQueue.empty()) {
        return std::nullopt;
    }
    MeshResult result = m_meshResultsQueue.front();
    m_meshResultsQueue.pop();
    return result;
}

bool VulkanThreadManager::hasMeshResults() {
    std::unique_lock<std::mutex> lock(m_resultsQueueMutex);
    return !m_meshResultsQueue.empty();
}

bool VulkanThreadManager::isIdle() const {
    std::unique_lock<std::mutex> lock(m_workQueueMutex);
    // The manager is idle if the work queue is empty AND no job is currently being processed.
    return m_meshWorkQueue.empty() && !m_isMeshing.load();
}

void VulkanThreadManager::mesherThreadLoop() {
    while (!m_stopMesherThread.load()) {
        glm::ivec3 chunkCoordToMesh;

        // Wait for a job to become available
        {
            std::unique_lock<std::mutex> lock(m_workQueueMutex);
            m_workQueueCV.wait(lock, [this] {
                return !m_meshWorkQueue.empty() || m_stopMesherThread.load();
            });

            if (m_stopMesherThread.load()) {
                break; // Exit loop if stop is requested
            }

            // Set the flag to indicate we are now processing a job, BEFORE releasing the lock.
            // This closes the race condition window with the isIdle() check.
            m_isMeshing.store(true);
            chunkCoordToMesh = m_meshWorkQueue.front();
            m_meshWorkQueue.pop();
        } // Lock is released here

        // --- Perform the meshing task ---
        // This part is outside the lock to avoid blocking the main thread while meshing.
        std::shared_ptr<const Chunk> chunk_sptr = m_world.getChunk(chunkCoordToMesh);
        if (chunk_sptr) {
            ModelData meshData = ChunkMesher::generateMesh(
                chunkCoordToMesh,
                *chunk_sptr,
                m_world,
                m_resourceManager,
                m_blockRegistry
            );

            // Push the result to the results queue
            {
                std::unique_lock<std::mutex> lock(m_resultsQueueMutex);
                m_meshResultsQueue.push({chunkCoordToMesh, std::move(meshData)});
            }
        } else {
            // If the chunk was unloaded between being queued and being processed,
            // we can simply drop this task. The main thread will handle removing
            // any old render data. We can optionally push an empty result to
            // signal completion if needed, but for now, we'll just ignore it.
            // For example:
            // {
            //     std::unique_lock<std::mutex> lock(m_resultsQueueMutex);
            //     m_meshResultsQueue.push({chunkCoordToMesh, ModelData{}});
            // }
        }

        // Clear the flag now that we are done with the job
        m_isMeshing.store(false);
    }
    std::cout << "VulkanThreadManager: Mesher thread loop exited." << std::endl;
}