#include "SimplePerlinGenerator.hpp"
#include "../Chunk.hpp"
#include "../../block/Blocks.hpp"
#include <glm/gtc/noise.hpp>
#include <array>
#include <cstring> // For std::memcpy

// Terrain generation parameters (Copied from Chunk.hpp/cpp to keep this generator self-contained)
constexpr int SEED_COORD_SCALE = 10000; 
constexpr double TERRAIN_FREQUENCY = 0.01; 
constexpr double TERRAIN_AMPLITUDE = 20.0; 
constexpr int DIRT_LAYER_THICKNESS = 3;   

void SimplePerlinGenerator::generateChunk(Chunk& chunk, int64_t worldSeed) {
    glm::ivec3 chunkCoord = chunk.getChunkCoord();
    
    // Use a temporary buffer on the stack to avoid holding the chunk lock during calculation
    std::array<uint16_t, CHUNK_VOLUME> tempBlockData;
    bool hasNonAirBlock = false;

    const int baseSurfaceAbsoluteY = CHUNK_SIDE_LENGTH / 2;

    /******** Pre-calculate a perlin noise offset from the world seed ********/

    // 1. Split the 64-bit world seed into two 32-bit integer components
    // This ensures 0 seed results in (0, 0) offset, and different seeds produce different offsets
    const int32_t gridX = static_cast<int32_t>(worldSeed >> 32);
    const int32_t gridY = static_cast<int32_t>(worldSeed & 0xFFFFFFFF);

    // 2. Scale these components to get a larger offset, then convert to double and scale by TERRAIN_FREQUENCY
    // This prevents terrain similarities across different seeds, while also properly preparing to add to
    // chunk coordinate such that precision loss is minimized.
    glm::dvec2 seedOffset(
        static_cast<double>(static_cast<int64_t>(gridX) * SEED_COORD_SCALE) * TERRAIN_FREQUENCY,
        static_cast<double>(static_cast<int64_t>(gridY) * SEED_COORD_SCALE) * TERRAIN_FREQUENCY
    );

    /******** Pre-calculate the chunk's origin contribution to the Perlin input ********/

    // 1. This uses 64-bit integers for the multiplication to prevent overflow, then converts
    // the result to double. This is safe as the result fits within a double's integer precision.
    glm::dvec2 chunkOriginPerlinInput(
        static_cast<double>(static_cast<int64_t>(chunkCoord.x) * CHUNK_SIDE_LENGTH) * TERRAIN_FREQUENCY,
        static_cast<double>(static_cast<int64_t>(chunkCoord.z) * CHUNK_SIDE_LENGTH) * TERRAIN_FREQUENCY
    );

    // 2. Add the seed offset to get the final chunk origin Perlin input
    chunkOriginPerlinInput += seedOffset;

    // Also get the absolute Y origin of the chunk using safe 64-bit arithmetic.
    const int64_t chunkWorldOriginY = static_cast<int64_t>(chunkCoord.y) * CHUNK_SIDE_LENGTH;

    /******** LOCAL COORDINATE LOOP ********/
    
    for (int lx = 0; lx < CHUNK_SIDE_LENGTH; ++lx) {
        for (int lz = 0; lz < CHUNK_SIDE_LENGTH; ++lz) {

            // Calculate the final Perlin input by adding the scaled local offset.
            // This maintains high precision by keeping the floating point values small.
            glm::dvec2 perlinInput = chunkOriginPerlinInput + glm::dvec2(static_cast<double>(lx) * TERRAIN_FREQUENCY, static_cast<double>(lz) * TERRAIN_FREQUENCY);
            double noiseValue = glm::perlin(perlinInput);
            int surfaceTopAbsoluteY = baseSurfaceAbsoluteY + static_cast<int>(noiseValue * TERRAIN_AMPLITUDE);

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

                tempBlockData[Chunk::localToIndex(lx, ly, lz)] = blockID;
                if (blockID != Blocks::AIR_ID) {
                    hasNonAirBlock = true;
                }
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