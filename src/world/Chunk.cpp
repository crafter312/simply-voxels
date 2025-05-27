#include "Chunk.hpp"
#include <stdexcept> // For std::out_of_range (if strict bounds checking is added)
#include <glm/gtc/noise.hpp> // For glm::perlin (already in hpp but good for explicitness)
#include <iostream>  // For debugging (optional)
#include <mutex>     // For std::unique_lock

#include "../block/Blocks.hpp" // For centralized block ID definitions

Chunk::Chunk(glm::ivec3 chunkCoord)
    : m_chunkCoord(chunkCoord),
      m_blocks(nullptr), // Start with no block data allocated
      m_isAllAir(true),  // Assume all air initially
      m_isDirty(false),   // New chunks are not dirty, only once generated or modified
      m_loadState(LoadState::UNINITIALIZED)
      // m_isGenerated is default-initialized for std::atomic<bool> to false
{
    //std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] CONSTRUCTOR called." << std::endl;
}

Chunk::~Chunk() {
    // std::unique_ptr will automatically handle deallocation of m_blocks if it was allocated.
    // glm::ivec3 worldPos = getWorldPosition(); // Calculate before potential destruction output
    // std::cout << "Chunk destroyed at world coordinates: " << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << " (Chunk Coords: " << m_chunkCoord.x << ", " << m_chunkCoord.y << ", " << m_chunkCoord.z << ")" << std::endl;
}

// Move constructor
Chunk::Chunk(Chunk&& other) noexcept
    : m_chunkCoord(other.m_chunkCoord),
      m_blocks(std::move(other.m_blocks)), // Transfer ownership of the block data
      m_isAllAir(other.m_isAllAir),
      m_isDirty(other.m_isDirty),
      m_loadState(other.m_loadState.load(std::memory_order_relaxed)), // Copy atomic state
      m_wasLoadedFromFile(other.m_wasLoadedFromFile) // Move the new flag
      // m_isGenerated: std::atomic is not trivially move-constructible in the same way.
      // We need to load from other and store into this.
{
    m_isGenerated.store(other.m_isGenerated.load(std::memory_order_relaxed), std::memory_order_relaxed);
    // other's m_isGenerated can be left or reset, typically reset if it's a true move.
    other.m_isGenerated.store(false, std::memory_order_relaxed);
    other.m_loadState.store(LoadState::UNINITIALIZED, std::memory_order_relaxed); // Reset other's load state
}

// Move assignment operator
Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        m_chunkCoord = other.m_chunkCoord;
        m_blocks = std::move(other.m_blocks); // Transfer ownership
        m_isAllAir = other.m_isAllAir;
        m_isDirty = other.m_isDirty;
        m_isGenerated.store(other.m_isGenerated.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_loadState.store(other.m_loadState.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_wasLoadedFromFile = other.m_wasLoadedFromFile; // Move the new flag
        
        // Reset other's relevant state if needed (unique_ptr is handled by move)
        other.m_isGenerated.store(false, std::memory_order_relaxed);
        other.m_wasLoadedFromFile = false; // Reset other's flag
        other.m_loadState.store(LoadState::UNINITIALIZED, std::memory_order_relaxed);
    }
    return *this;
}

void Chunk::allocateBlockStorage() {
    // CRITICAL: DO NOT lock m_data_mutex here.
    // The caller (setBlock) is responsible for holding the lock.
    if (!m_blocks) { // Only allocate if not already allocated
        m_blocks = std::make_unique<std::array<uint16_t, CHUNK_VOLUME>>();
        m_blocks->fill(Blocks::AIR_ID); // Initialize all new blocks to air
        m_isAllAir = false; // No longer all air once storage is allocated
    }
}

uint16_t Chunk::getBlock(int localX, int localY, int localZ) const {
    std::shared_lock<std::shared_mutex> lock(m_data_mutex);
    if (localX < 0 || localX >= CHUNK_SIDE_LENGTH ||
        localY < 0 || localY >= CHUNK_SIDE_LENGTH ||
        localZ < 0 || localZ >= CHUNK_SIDE_LENGTH) {
        // Potentially handle out-of-bounds access, e.g., by returning AIR_BLOCK_ID
        // or throwing an error. For now, let's assume valid local coordinates
        // or rely on the caller to ensure this.
        // For world-level getBlock, this would query neighbor chunks.        
        return Blocks::AIR_ID; // Or throw
    }

    if (m_isAllAir || !m_blocks) {
        return Blocks::AIR_ID;
    }
    return (*m_blocks)[localToIndex(localX, localY, localZ)];
}

void Chunk::setBlock(int localX, int localY, int localZ, uint16_t blockID) {
    if (localX < 0 || localX >= CHUNK_SIDE_LENGTH ||
        localY < 0 || localY >= CHUNK_SIDE_LENGTH ||
        localZ < 0 || localZ >= CHUNK_SIDE_LENGTH) {
        // Cannot set a block outside the chunk's bounds from within the chunk itself.
        // This should be handled by a world manager.
        // For now, we can ignore or log an error.
        std::cerr << "Attempted to set block outside of chunk bounds!" << std::endl;
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_data_mutex);
    if (m_isAllAir && blockID == Blocks::AIR_ID) {
        // Setting air in an all-air chunk, no change needed.
        return;
    }

    if (m_isAllAir && blockID != Blocks::AIR_ID) {
        // First non-air block being placed in an all-air chunk.
        allocateBlockStorage(); // This also sets m_isAllAir to false.
    }
    // By this point, if blockID is non-air, m_blocks is guaranteed to be allocated.
    // If blockID is air, and m_blocks was already allocated, we just set it.

    // Check if the block actually changed before marking as dirty or needing save
    uint16_t oldBlockID = Blocks::AIR_ID; // Assume air if m_blocks is null
    if (m_blocks) {
        oldBlockID = (*m_blocks)[localToIndex(localX, localY, localZ)];
    }

    // If the block is the same as before, no need to do anything
    if (oldBlockID == blockID) {
        return; // No change
    }

    if (m_blocks) { // Should always be true if we are setting a non-air block or were not all-air
        (*m_blocks)[localToIndex(localX, localY, localZ)] = blockID;
    }
    
    // If we set a block (even to air in a non-all-air chunk), the chunk is dirty.
    m_isDirty = true;

    // Future optimization: If blockID is AIR_BLOCK_ID, check if the chunk became all-air.
    // If so, m_blocks.reset(); m_isAllAir = true;
}

glm::ivec3 Chunk::getWorldPosition() const {
    return glm::ivec3(m_chunkCoord.x * CHUNK_SIDE_LENGTH,
                      m_chunkCoord.y * CHUNK_SIDE_LENGTH,
                      m_chunkCoord.z * CHUNK_SIDE_LENGTH);
}
bool Chunk::isDirty() const { 
    std::shared_lock<std::shared_mutex> lock(m_data_mutex);
    return m_isDirty; 
}
void Chunk::setDirty(bool dirty) { 
    std::unique_lock<std::shared_mutex> lock(m_data_mutex);
    m_isDirty = dirty; 
}

bool Chunk::isAllAir() const {
    std::shared_lock<std::shared_mutex> lock(m_data_mutex);
    // std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] isAllAir() returning " << m_isAllAir << std::endl;
    return m_isAllAir; 
}

void Chunk::generate() {
    setLoadState(LoadState::GENERATING_PROCEDURALLY); // Set state at the beginning

    //std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] STARTING generation." << std::endl;
    // Original Perlin Noise Terrain Generation:
    // Terrain generation parameters
    // const double TERRAIN_FREQUENCY = 0.01; // Defined in Chunk.hpp
    // const double TERRAIN_AMPLITUDE = 20.0; // Defined in Chunk.hpp

    const int baseSurfaceAbsoluteY = CHUNK_SIDE_LENGTH / 2; // An arbitrary "sea level" or average ground height in absolute Y.
                                                // Adjust this if your world's "ground" is typically higher or lower.
                                                // For chunks at y=0, this means surface is around local y=8.

    glm::ivec3 chunkWorldOrigin = getWorldPosition(); // Absolute world coordinates of this chunk's origin (0,0,0 local)

    // Temporary storage for block data. CHUNK_VOLUME is typically small enough (e.g., 16*256*16 = 65536)
    // for a std::array<uint16_t, CHUNK_VOLUME> (128KB) to be on the stack.
    // If CHUNK_VOLUME were much larger, std::vector or heap allocation might be preferred.
    std::array<uint16_t, CHUNK_VOLUME> tempBlockData;
    // tempBlockData.fill(Blocks::AIR_ID); // Initialize all to air, though loop will set all.

    //std::cout << "  Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] World Y range: [" << chunkWorldOrigin.y << " to " << chunkWorldOrigin.y + CHUNK_HEIGHT - 1 << "]" << std::endl;
    bool hasNonAirBlock = false;

    for (int lx = 0; lx < CHUNK_SIDE_LENGTH; ++lx) {
        for (int lz = 0; lz < CHUNK_SIDE_LENGTH; ++lz) {
            // Calculate absolute world X and Z for the current block column
            double absoluteWorldX = static_cast<double>(chunkWorldOrigin.x + lx);
            double absoluteWorldZ = static_cast<double>(chunkWorldOrigin.z + lz);

            double noiseValue = glm::perlin(glm::dvec2(absoluteWorldX * TERRAIN_FREQUENCY, absoluteWorldZ * TERRAIN_FREQUENCY));
            int surfaceTopAbsoluteY = baseSurfaceAbsoluteY + static_cast<int>(noiseValue * TERRAIN_AMPLITUDE);

            for (int ly = 0; ly < CHUNK_SIDE_LENGTH; ++ly) {
                int currentBlockAbsoluteY = chunkWorldOrigin.y + ly; // Absolute Y of the current block layer
                uint16_t blockID;

                if (currentBlockAbsoluteY < surfaceTopAbsoluteY) {
                    if (currentBlockAbsoluteY >= surfaceTopAbsoluteY - DIRT_LAYER_THICKNESS) {
                        blockID = Blocks::DIRT_ID;
                    } else {
                        blockID = Blocks::STONE_ID;
                    }
                } else {
                    blockID = Blocks::AIR_ID;
                }

                tempBlockData[localToIndex(lx, ly, lz)] = blockID;
                if (blockID != Blocks::AIR_ID) {
                    hasNonAirBlock = true;
                }
            }
        }
    }

    // Now, update the chunk's actual block data under a single lock
    {
        std::unique_lock<std::shared_mutex> lock(m_data_mutex);
        if (hasNonAirBlock) {
            if (!m_blocks) { // Allocate if it doesn't exist (expected for a new chunk)
                m_blocks = std::make_unique<std::array<uint16_t, CHUNK_VOLUME>>();
            }
            *m_blocks = tempBlockData; // Copy the generated data
            m_isAllAir = false;
        } else {
            m_blocks.reset(); // Deallocate if it was all air (and ensure it's null)
            m_isAllAir = true;
        }
        m_isDirty = true; // Generation makes the chunk dirty, requiring a mesh rebuild.
    }
    // Set state to READY and then mark as generated after data is populated
    setLoadState(LoadState::READY);
    markGenerated(); // Mark this chunk as having completed its initial generation
}

void Chunk::setAllBlocks(const std::array<uint16_t, CHUNK_VOLUME>& new_blocks_data) {
    std::unique_lock<std::shared_mutex> lock(m_data_mutex);
    if (!m_blocks) {
        // allocateBlockStorage already sets m_isAllAir to false
        // and initializes to AIR_ID, but we'll overwrite immediately.
        // It's crucial that allocateBlockStorage is called without the lock held
        // if it were to call other methods that also lock. However, its current
        // implementation is simple enough. For safety, we could unlock and relock,
        // but given its current state, this is okay.
        // The primary purpose of allocateBlockStorage is to create the m_blocks unique_ptr.
        m_blocks = std::make_unique<std::array<uint16_t, CHUNK_VOLUME>>();
    }
    *m_blocks = new_blocks_data; // Direct copy
    m_isAllAir = false;
    m_isDirty = false; // Loaded from file, not dirty initially
    setLoadState(LoadState::READY); // Set state to READY first
    markGenerated(); // Data is now populated, mark as generated
}

void Chunk::setAllAir() {
    std::unique_lock<std::shared_mutex> lock(m_data_mutex);
    m_blocks.reset(); // Deallocate block storage
    m_isAllAir = true;
    m_isDirty = false; // Loaded from file, not dirty initially
    setLoadState(LoadState::READY); // Set state to READY first
    markGenerated(); // All-air is also considered "generated" or "loaded" for meshing
}

void Chunk::performLockedWrite(std::function<void(uint16_t* block_data_buffer, size_t buffer_capacity_elements)> writer_action) {
    std::unique_lock<std::shared_mutex> lock(m_data_mutex);

    // Ensure block storage is allocated.
    // allocateBlockStorage() will create m_blocks if it's nullptr
    // and set m_isAllAir to false.
    if (!m_blocks) {
        allocateBlockStorage(); // This sets m_isAllAir = false internally
    } else if (m_isAllAir) {
        // If m_blocks exists but chunk was marked all_air (e.g., after setAllAir()),
        // then preparing to load data means it's no longer all_air.
        m_isAllAir = false;
    }
    
    // Call the provided writer function with the buffer and its capacity.
    // m_blocks is guaranteed to be non-null here.
    writer_action(m_blocks->data(), CHUNK_VOLUME);
}

void Chunk::performLockedRead(std::function<void(const uint16_t* block_data_buffer, size_t buffer_capacity_elements)> reader_action) const {
    std::shared_lock<std::shared_mutex> lock(m_data_mutex);

    if (m_isAllAir || !m_blocks) {
        // If the chunk is all air or has no block storage, pass nullptr to the reader.
        reader_action(nullptr, 0);
    } else {
        // Pass a const pointer to the block data and its capacity.
        reader_action(m_blocks->data(), CHUNK_VOLUME);
    }
}

Chunk::LoadState Chunk::getLoadState() const {
    return m_loadState.load(std::memory_order_acquire);
}

void Chunk::setLoadState(LoadState newState) {
    m_loadState.store(newState, std::memory_order_release);
}