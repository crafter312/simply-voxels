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
      m_isDirty(false)   // New chunks are not dirty, only once generated or modified
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
      m_wasLoadedFromFile(other.m_wasLoadedFromFile) // Move the new flag
      // m_isGenerated: std::atomic is not trivially move-constructible in the same way.
      // We need to load from other and store into this.
{
    m_isGenerated.store(other.m_isGenerated.load(std::memory_order_relaxed), std::memory_order_relaxed);
    // other's m_isGenerated can be left or reset, typically reset if it's a true move.
    other.m_isGenerated.store(false, std::memory_order_relaxed);
}

// Move assignment operator
Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        m_chunkCoord = other.m_chunkCoord;
        m_blocks = std::move(other.m_blocks); // Transfer ownership
        m_isAllAir = other.m_isAllAir;
        m_isDirty = other.m_isDirty;
        m_isGenerated.store(other.m_isGenerated.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_wasLoadedFromFile = other.m_wasLoadedFromFile; // Move the new flag
        
        // Reset other's relevant state if needed (unique_ptr is handled by move)
        other.m_isGenerated.store(false, std::memory_order_relaxed);
        other.m_wasLoadedFromFile = false; // Reset other's flag
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
    if (localX < 0 || localX >= CHUNK_WIDTH ||
        localY < 0 || localY >= CHUNK_HEIGHT ||
        localZ < 0 || localZ >= CHUNK_DEPTH) {
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
    if (localX < 0 || localX >= CHUNK_WIDTH ||
        localY < 0 || localY >= CHUNK_HEIGHT ||
        localZ < 0 || localZ >= CHUNK_DEPTH) {
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
    return glm::ivec3(m_chunkCoord.x * CHUNK_WIDTH,
                      m_chunkCoord.y * CHUNK_HEIGHT,
                      m_chunkCoord.z * CHUNK_DEPTH);
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

std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> Chunk::getBlockDataSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(m_data_mutex);
    if (m_isAllAir || !m_blocks) {
        // std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] getBlockDataSnapshot() returning nullptr (all_air or no_blocks)." << std::endl;
        return nullptr; // Or return an empty (all-air) snapshot if preferred by mesher
    }
    // Create a copy of the block data
    auto snapshot = std::make_unique<std::array<uint16_t, CHUNK_VOLUME>>(*m_blocks);
    return snapshot;
}

void Chunk::generate() {
    //std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] STARTING generation." << std::endl;
    // Original Perlin Noise Terrain Generation:
    // Terrain generation parameters
    // const double TERRAIN_FREQUENCY = 0.01; // Defined in Chunk.hpp
    // const double TERRAIN_AMPLITUDE = 20.0; // Defined in Chunk.hpp

    const int baseSurfaceAbsoluteY = CHUNK_HEIGHT / 2; // An arbitrary "sea level" or average ground height in absolute Y.
                                                // Adjust this if your world's "ground" is typically higher or lower.
                                                // For chunks at y=0, this means surface is around local y=8.

    glm::ivec3 chunkWorldOrigin = getWorldPosition(); // Absolute world coordinates of this chunk's origin (0,0,0 local)

    // No need to call allocateBlockStorage() explicitly at the start.
    // setBlock() will call it if a non-air block is placed in an all-air chunk.
    // m_isAllAir is true by default for a new chunk.

    //std::cout << "  Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] World Y range: [" << chunkWorldOrigin.y << " to " << chunkWorldOrigin.y + CHUNK_HEIGHT - 1 << "]" << std::endl;
    int intendedNonAirBlocks = 0;
    for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            // Calculate absolute world X and Z for the current block column
            // Using double for precision with Perlin noise, especially with large coordinates
            double absoluteWorldX = static_cast<double>(chunkWorldOrigin.x + lx);
            double absoluteWorldZ = static_cast<double>(chunkWorldOrigin.z + lz);

            // Generate Perlin noise value. glm::perlin for dvec2 returns double in [-1, 1]
            double noiseValue = glm::perlin(glm::dvec2(absoluteWorldX * TERRAIN_FREQUENCY, absoluteWorldZ * TERRAIN_FREQUENCY));

            // Calculate surface height for this column
            // This is an absolute Y coordinate in the world
            int surfaceTopAbsoluteY = baseSurfaceAbsoluteY + static_cast<int>(noiseValue * TERRAIN_AMPLITUDE);
            
            if (lx == 0 && lz == 0) { // Log for one column for brevity
                 //std::cout << "  Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] Col (0,0): surfaceTopAbsoluteY = " << surfaceTopAbsoluteY 
                 //          << " (base: " << baseSurfaceAbsoluteY << ", noiseVal: " << noiseValue << ", amplitudeEffect: " << static_cast<int>(noiseValue * TERRAIN_AMPLITUDE) << ")" << std::endl;
            }

            for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
                int currentBlockAbsoluteY = chunkWorldOrigin.y + ly; // Absolute Y of the current block layer

                if (currentBlockAbsoluteY < surfaceTopAbsoluteY) {
                    // This block is solid (not air)
                    if (currentBlockAbsoluteY >= surfaceTopAbsoluteY - DIRT_LAYER_THICKNESS) {
                        // Within the dirt layer (topmost solid blocks)
                        setBlock(lx, ly, lz, Blocks::DIRT_ID);
                    } else {
                        // Below the dirt layer, it's stone
                        setBlock(lx, ly, lz, Blocks::STONE_ID);
                    }
                    intendedNonAirBlocks++;
                } else {
                    setBlock(lx, ly, lz, Blocks::AIR_ID);
                }
            }
        }
    }

    //std::cout << "  Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] Intended non-air blocks during generation: " << intendedNonAirBlocks << std::endl;
    
    // After all blocks are set, accurately determine if the chunk is all air.
    // This is important because setBlock(AIR_BLOCK_ID) in an all-air chunk doesn't allocate.
    // If non-air blocks were placed, m_blocks would be allocated.
    // If it was allocated then filled with air, we should deallocate.
    // This critical section needs to be protected.
    {
        std::unique_lock<std::shared_mutex> lock(m_data_mutex);
        bool finalIsAllAirCheck = true; // Use a temporary for the check
        if (m_blocks) { // Only check if m_blocks was ever allocated
            for (const auto& blockID_val : *m_blocks) { // Renamed to avoid conflict
                if (blockID_val != Blocks::AIR_ID) {
                    finalIsAllAirCheck = false;
                    break;
                }
            }
            if (finalIsAllAirCheck) {
                m_blocks.reset(); // Deallocate if it ended up all air
            }
        }
        m_isAllAir = finalIsAllAirCheck; // Set the final state under lock
        m_isDirty = true; // Mark the chunk as dirty so its mesh will be rebuilt, now under lock.
    }
    //std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] FINISHED generation, calling markGenerated(). isAllAir: " << m_isAllAir << std::endl;
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
}

void Chunk::setAllAir() {
    std::unique_lock<std::shared_mutex> lock(m_data_mutex);
    m_blocks.reset(); // Deallocate block storage
    m_isAllAir = true;
    m_isDirty = false; // Loaded from file, not dirty initially
}