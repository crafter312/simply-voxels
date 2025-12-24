#include "SimpleFlatGenerator.hpp"
#include "../Chunk.hpp"
#include "../../block/Blocks.hpp"
#include <glm/gtc/noise.hpp>
#include <array>
#include <cstring> // For std::memcpy

// Terrain generation parameters (Copied from Chunk.hpp/cpp to keep this generator self-contained)
constexpr int SEED_COORD_SCALE = 10000;
constexpr int DIRT_LAYER_THICKNESS = 3;   

void SimpleFlatGenerator::generateChunk(Chunk& chunk, int64_t worldSeed) {
    glm::ivec3 chunkCoord = chunk.getChunkCoord();
    
    // Use a temporary buffer on the stack to avoid holding the chunk lock during calculation
    std::array<uint16_t, CHUNK_VOLUME> tempBlockData;
    bool hasNonAirBlock = false;

    // Simple flat world only requires absolute Y origin of the chunk using safe 64-bit arithmetic.
    const int64_t chunkWorldOriginY = static_cast<int64_t>(chunkCoord.y) * CHUNK_SIDE_LENGTH;

    const int surfaceTopAbsoluteY = 64; // Flat world surface height

    /******** LOCAL COORDINATE LOOP ********/
    
    for (int ly = 0; ly < CHUNK_SIDE_LENGTH; ++ly) {
        int64_t currentBlockAbsoluteY = chunkWorldOriginY + ly;

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

        for (int lx = 0; lx < CHUNK_SIDE_LENGTH; ++lx) {
            for (int lz = 0; lz < CHUNK_SIDE_LENGTH; ++lz) {
                tempBlockData[Chunk::localToIndex(lx, ly, lz)] = blockID;
                if (blockID != Blocks::AIR_ID) hasNonAirBlock = true;
            }
        }
    }

    /******** WRITE DATA TO CHUNK ********/

    // Early out for all-air chunks
    if (!hasNonAirBlock) {
        chunk.setAllAir();
        chunk.markGenerated();
        return;
    }

    // If we have blocks, allocate storage and copy, mark flags
    chunk.performLockedWrite([&](uint16_t* buffer, size_t capacity) {
        std::memcpy(buffer, tempBlockData.data(), capacity * sizeof(uint16_t)); // we know capacity is CHUNK_VOLUME
    });
    chunk.setDirty(true);
    chunk.markGenerated();
}