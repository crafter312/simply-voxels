#ifndef REGION_MANAGER_HPP
#define REGION_MANAGER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <glm/vec3.hpp>

// Forward declaration
class Chunk;

namespace WorldSave {

constexpr int REGION_WIDTH_IN_CHUNKS = 32;
constexpr int REGION_HEIGHT_IN_CHUNKS = 32; // For fully 3D regions
constexpr int REGION_DEPTH_IN_CHUNKS = 32;
constexpr int REGION_VOLUME_IN_CHUNKS = REGION_WIDTH_IN_CHUNKS * REGION_HEIGHT_IN_CHUNKS * REGION_DEPTH_IN_CHUNKS;

constexpr char REGION_FILE_MAGIC[4] = {'R', 'G', 'N', '1'}; // Magic number for region files
constexpr uint8_t REGION_FILE_VERSION = 1;

struct RegionFileHeader {
    char magic[4];              // Should match REGION_FILE_MAGIC
    uint8_t version;            // Should match REGION_FILE_VERSION
    uint8_t region_dim_x_chunks; // Expected: REGION_WIDTH_IN_CHUNKS
    uint8_t region_dim_y_chunks; // Expected: REGION_HEIGHT_IN_CHUNKS
    uint8_t region_dim_z_chunks; // Expected: REGION_DEPTH_IN_CHUNKS
    uint32_t reserved[3];       // Padding/Future use to make header 20 bytes
};

struct ChunkIndexEntry {
    uint32_t offset = 0;              // Offset in bytes from the start of the file to the chunk data
    uint32_t compressed_length = 0;   // Length of the compressed chunk data in bytes
    uint32_t uncompressed_length = 0; // Expected length of the uncompressed chunk data (CHUNK_VOLUME * sizeof(uint16_t))
    
    // Flags:
    // bit 0: isAllAir
    // bit 1: isCompressed (currently, if not allAir, assumed compressed with LZ4)
    // ... other bits reserved
    uint8_t flags = 0;
    
    uint8_t reserved[3] = {0};          // Padding to make struct size 16 bytes

    ChunkIndexEntry() = default; // Ensure members are zero/default initialized

    bool isAllAir() const { return flags & 0x01; }
    void setAllAir(bool val) {
        if (val) flags |= 0x01;
        else flags &= ~0x01;
    }

    // bool isCompressed() const { return flags & 0x02; } // Example for future use
    // void setCompressed(bool val) {
    //     if (val) flags |= 0x02;
    //     else flags &= ~0x02;
    // }
};

class RegionManager {
public:
    RegionManager(const std::string& base_save_path);

    // Attempts to load chunk data from a region file.
    // - Modifies the passed 'chunk' object with loaded data.
    // - Returns true if the chunk was successfully loaded from file, false otherwise
    //   (e.g., file not found, chunk not saved in file, read error, decompression error).
    bool loadChunkFromFile(Chunk& chunk);

    // Attempts to save chunk data to a region file.
    // - Compresses the chunk data before saving.
    // - Returns true if the chunk was successfully saved, false otherwise.
    bool saveChunkToFile(Chunk& chunk_ref);

private:
    std::string m_base_save_path;
    std::string getRegionFilePath(const glm::ivec3& region_coord) const;
    size_t getLocalChunkIndex(const glm::ivec3& local_chunk_coord_in_region) const;
};

} // namespace WorldSave
#endif // REGION_MANAGER_HPP