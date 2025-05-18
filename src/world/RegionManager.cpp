#include "RegionManager.hpp"
#include "Chunk.hpp" // For CHUNK_VOLUME, CHUNK_WIDTH etc. and Chunk class
#include "../block/Blocks.hpp" // For Blocks::AIR_ID (though Chunk handles this internally)

#include <fstream>
#include <vector>
#include <iostream> // For error logging
#include <cstring>  // For memcpy, strcmp
#include <filesystem> // For creating directories

// Include LZ4 header
#include <lz4.h>

namespace WorldSave {

RegionManager::RegionManager(const std::string& base_save_path)
    : m_base_save_path(base_save_path) {
    // Ensure the base save path itself exists (this is now the direct container for region files)
    std::filesystem::path save_dir_path = std::filesystem::path(m_base_save_path);
    if (!std::filesystem::exists(save_dir_path)) {
        if (!std::filesystem::create_directories(save_dir_path)) {
            std::cerr << "Error: Could not create save directory: " << save_dir_path << std::endl;
            // Depending on desired strictness, could throw an exception here
        }
    }
}

std::string RegionManager::getRegionFilePath(const glm::ivec3& region_coord) const {
    std::filesystem::path file_path = std::filesystem::path(m_base_save_path) /
                                      ("r." + std::to_string(region_coord.x) + // Region files are directly in m_base_save_path
                                       "." + std::to_string(region_coord.y) +
                                       "." + std::to_string(region_coord.z) + ".dat");
    return file_path.string();
}

size_t RegionManager::getLocalChunkIndex(const glm::ivec3& local_chunk_coord_in_region) const {
    // Assuming local_chunk_coord_in_region components are already modulo REGION_DIM_IN_CHUNKS
    return static_cast<size_t>(local_chunk_coord_in_region.x +
                               local_chunk_coord_in_region.y * REGION_WIDTH_IN_CHUNKS +
                               local_chunk_coord_in_region.z * REGION_WIDTH_IN_CHUNKS * REGION_HEIGHT_IN_CHUNKS);
}

bool RegionManager::loadChunkFromFile(Chunk& chunk_ref) {
    glm::ivec3 chunk_coord = chunk_ref.getChunkCoord();

    // Calculate region coordinates
    glm::ivec3 region_coord(
        static_cast<int>(std::floor(static_cast<float>(chunk_coord.x) / REGION_WIDTH_IN_CHUNKS)),
        static_cast<int>(std::floor(static_cast<float>(chunk_coord.y) / REGION_HEIGHT_IN_CHUNKS)),
        static_cast<int>(std::floor(static_cast<float>(chunk_coord.z) / REGION_DEPTH_IN_CHUNKS))
    );

    // Calculate local chunk coordinates within the region
    glm::ivec3 local_chunk_coord(
        chunk_coord.x - region_coord.x * REGION_WIDTH_IN_CHUNKS,
        chunk_coord.y - region_coord.y * REGION_HEIGHT_IN_CHUNKS,
        chunk_coord.z - region_coord.z * REGION_DEPTH_IN_CHUNKS
    );

    std::string file_path_str = getRegionFilePath(region_coord);
    std::ifstream region_file(file_path_str, std::ios::binary);

    if (!region_file.is_open()) {
        // std::cout << "Region file not found (or cannot be opened): " << file_path_str << " for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << std::endl;
        return false; // File doesn't exist or couldn't be opened, so chunk data isn't saved here.
    }

    // 1. Read and validate RegionFileHeader
    RegionFileHeader header;
    region_file.read(reinterpret_cast<char*>(&header), sizeof(RegionFileHeader));
    if (region_file.gcount() != sizeof(RegionFileHeader)) {
        std::cerr << "Error reading region file header from: " << file_path_str << std::endl;
        return false;
    }

    if (std::strncmp(header.magic, REGION_FILE_MAGIC, 4) != 0 || header.version != REGION_FILE_VERSION) {
        std::cerr << "Region file header mismatch (magic/version) in: " << file_path_str << std::endl;
        return false;
    }
    if (header.region_dim_x_chunks != REGION_WIDTH_IN_CHUNKS ||
        header.region_dim_y_chunks != REGION_HEIGHT_IN_CHUNKS ||
        header.region_dim_z_chunks != REGION_DEPTH_IN_CHUNKS) {
        std::cerr << "Region file dimension mismatch in: " << file_path_str << std::endl;
        return false;
    }

    // 2. Calculate local index and seek to ChunkIndexEntry
    size_t chunk_idx_in_region = getLocalChunkIndex(local_chunk_coord);
    size_t index_table_offset = sizeof(RegionFileHeader);
    size_t target_entry_offset = index_table_offset + (chunk_idx_in_region * sizeof(ChunkIndexEntry));

    region_file.seekg(target_entry_offset);
    if (region_file.fail()) {
        std::cerr << "Error seeking to chunk index entry in: " << file_path_str << std::endl;
        return false;
    }

    // 3. Read ChunkIndexEntry
    ChunkIndexEntry entry;
    region_file.read(reinterpret_cast<char*>(&entry), sizeof(ChunkIndexEntry));
    if (region_file.gcount() != sizeof(ChunkIndexEntry)) {
        std::cerr << "Error reading chunk index entry from: " << file_path_str << std::endl;
        return false;
    }

    // 4. Process based on ChunkIndexEntry
    if (entry.isAllAir()) {
        chunk_ref.setAllAir(); // This also sets dirty to false
        chunk_ref.markGenerated(); // Mark as "loaded"
        // std::cout << "Loaded chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " as all_air from file." << std::endl;
        return true;
    }

    // If not allAir, but offset or length is 0, it means it's not actually saved (or an error in saving)
    if (entry.offset == 0 || entry.compressed_length == 0 || entry.uncompressed_length == 0) {
         //std::cout << "Chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " entry in region file indicates no data saved." << std::endl;
        return false; // Chunk data not present in the file
    }

    // Check if expected uncompressed length matches our chunk volume
    if (entry.uncompressed_length != CHUNK_VOLUME * sizeof(uint16_t)) {
        std::cerr << "Mismatch in uncompressed length for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z
                  << ". Expected: " << CHUNK_VOLUME * sizeof(uint16_t) << ", Got: " << entry.uncompressed_length << std::endl;
        return false;
    }

    // 5. Read compressed data
    std::vector<char> compressed_data(entry.compressed_length);
    region_file.seekg(entry.offset);
    if (region_file.fail()) {
        std::cerr << "Error seeking to chunk data in: " << file_path_str << std::endl;
        return false;
    }
    region_file.read(compressed_data.data(), entry.compressed_length);
    if (region_file.gcount() != static_cast<std::streamsize>(entry.compressed_length)) {
        std::cerr << "Error reading compressed chunk data from: " << file_path_str << std::endl;
        return false;
    }

    // 6. Decompress data
    std::vector<char> uncompressed_data_char_vec(entry.uncompressed_length);

    int decompressed_size = LZ4_decompress_safe(
        compressed_data.data(),
        uncompressed_data_char_vec.data(),
        static_cast<int>(entry.compressed_length),
        static_cast<int>(entry.uncompressed_length)
    );

    if (decompressed_size < 0) {
        std::cerr << "LZ4 decompression failed for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z
                  << " in file " << file_path_str << ". Error code: " << decompressed_size << std::endl;
        return false;
    }
    if (static_cast<uint32_t>(decompressed_size) != entry.uncompressed_length) {
        std::cerr << "LZ4 decompression size mismatch for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z
                  << ". Expected: " << entry.uncompressed_length << ", Got: " << decompressed_size << std::endl;
        return false;
    }

    // 7. Populate Chunk object
    // We need to cast the char vector to an array of uint16_t.
    // This assumes the data is correctly aligned and structured.
    std::array<uint16_t, CHUNK_VOLUME> block_data_array;
    // Ensure the size matches before memcpy to avoid buffer overflows
    if (uncompressed_data_char_vec.size() == sizeof(block_data_array)) {
        std::memcpy(block_data_array.data(), uncompressed_data_char_vec.data(), sizeof(block_data_array));
        chunk_ref.setAllBlocks(block_data_array); // This also sets dirty to false
        chunk_ref.markGenerated(); // Mark as "loaded"
        // std::cout << "Successfully loaded and decompressed chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " from file." << std::endl;
        return true;
    } else {
        std::cerr << "Decompressed data size does not match expected block array size for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << std::endl;
        return false;
    }
}

bool RegionManager::saveChunkToFile(Chunk& chunk_ref) {
    glm::ivec3 chunk_coord = chunk_ref.getChunkCoord();

    // Calculate region and local chunk coordinates
    glm::ivec3 region_coord(
        static_cast<int>(std::floor(static_cast<float>(chunk_coord.x) / REGION_WIDTH_IN_CHUNKS)),
        static_cast<int>(std::floor(static_cast<float>(chunk_coord.y) / REGION_HEIGHT_IN_CHUNKS)),
        static_cast<int>(std::floor(static_cast<float>(chunk_coord.z) / REGION_DEPTH_IN_CHUNKS))
    );
    glm::ivec3 local_chunk_coord(
        chunk_coord.x - region_coord.x * REGION_WIDTH_IN_CHUNKS,
        chunk_coord.y - region_coord.y * REGION_HEIGHT_IN_CHUNKS,
        chunk_coord.z - region_coord.z * REGION_DEPTH_IN_CHUNKS
    );

    std::string file_path_str = getRegionFilePath(region_coord);
    std::fstream region_file;

    // Try to open existing file, or create it if it doesn't exist
    region_file.open(file_path_str, std::ios::in | std::ios::out | std::ios::binary);
    if (!region_file.is_open()) {
        // File doesn't exist, create it and initialize
        region_file.open(file_path_str, std::ios::out | std::ios::binary | std::ios::trunc); // Create/truncate
        if (!region_file.is_open()) {
            std::cerr << "Error: Could not create region file for writing: " << file_path_str << std::endl;
            return false;
        }

        // Write header
        RegionFileHeader new_header;
        std::memcpy(new_header.magic, REGION_FILE_MAGIC, sizeof(REGION_FILE_MAGIC));
        new_header.version = REGION_FILE_VERSION;
        new_header.region_dim_x_chunks = REGION_WIDTH_IN_CHUNKS;
        new_header.region_dim_y_chunks = REGION_HEIGHT_IN_CHUNKS;
        new_header.region_dim_z_chunks = REGION_DEPTH_IN_CHUNKS;
        // new_header.reserved is already zeroed by default struct initialization if applicable, or do it manually
        std::memset(new_header.reserved, 0, sizeof(new_header.reserved));
        region_file.write(reinterpret_cast<const char*>(&new_header), sizeof(RegionFileHeader));

        // Write empty index table
        std::vector<ChunkIndexEntry> empty_index_table(REGION_VOLUME_IN_CHUNKS); // Uses default constructor for ChunkIndexEntry
        region_file.write(reinterpret_cast<const char*>(empty_index_table.data()), empty_index_table.size() * sizeof(ChunkIndexEntry));

        if (region_file.fail()) {
            std::cerr << "Error initializing new region file: " << file_path_str << std::endl;
            region_file.close();
            // Optionally delete the partially written file: std::filesystem::remove(file_path_str);
            return false;
        }
        region_file.close();

        // Reopen in r/w mode
        region_file.open(file_path_str, std::ios::in | std::ios::out | std::ios::binary);
        if (!region_file.is_open()) {
            std::cerr << "Error: Could not reopen region file for r/w: " << file_path_str << std::endl;
            return false;
        }
    } else {
        // File exists, read and validate header (optional, loadChunk does this, but good for save too)
        RegionFileHeader header_check;
        region_file.read(reinterpret_cast<char*>(&header_check), sizeof(RegionFileHeader));
        if (region_file.gcount() != sizeof(RegionFileHeader) ||
            std::strncmp(header_check.magic, REGION_FILE_MAGIC, 4) != 0 ||
            header_check.version != REGION_FILE_VERSION) {
            std::cerr << "Region file header mismatch on existing file (save): " << file_path_str << std::endl;
            region_file.close();
            return false;
        }
        // Seek back to start of file for subsequent operations if needed, or manage current position.
        // For now, we'll seek directly to the index entry.
    }

    // Get the index entry for this chunk
    size_t chunk_idx_in_region = getLocalChunkIndex(local_chunk_coord);
    size_t index_entry_disk_offset = sizeof(RegionFileHeader) + (chunk_idx_in_region * sizeof(ChunkIndexEntry));

    ChunkIndexEntry current_disk_entry;
    region_file.seekg(index_entry_disk_offset);
    if (region_file.fail()) {
         std::cerr << "Error seeking to index entry for save: " << file_path_str << std::endl;
         region_file.close();
         return false;
    }
    region_file.read(reinterpret_cast<char*>(&current_disk_entry), sizeof(ChunkIndexEntry));
    if (region_file.gcount() != sizeof(ChunkIndexEntry)) {
        std::cerr << "Error reading index entry for save: " << file_path_str << std::endl;
        region_file.close();
        return false;
    }

    ChunkIndexEntry entry_to_write = current_disk_entry; // Work on a copy

    if (chunk_ref.isAllAir()) {
        entry_to_write.setAllAir(true);
        entry_to_write.offset = 0;
        entry_to_write.compressed_length = 0;
        entry_to_write.uncompressed_length = 0;
    } else {
        auto block_data_snapshot_ptr = chunk_ref.getBlockDataSnapshot();
        if (!block_data_snapshot_ptr) {
            std::cerr << "Error: Chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z
                      << " is not all_air but returned no block data for saving." << std::endl;
            region_file.close();
            return false; // Inconsistent chunk state
        }
        const auto& block_data_array = *block_data_snapshot_ptr;

        const char* uncompressed_data_ptr = reinterpret_cast<const char*>(block_data_array.data());
        int uncompressed_data_size = static_cast<int>(CHUNK_VOLUME * sizeof(uint16_t));

        int max_compressed_size = LZ4_compressBound(uncompressed_data_size);
        std::vector<char> compressed_buffer(max_compressed_size);

        int actual_compressed_size = LZ4_compress_default(
            uncompressed_data_ptr,
            compressed_buffer.data(),
            uncompressed_data_size,
            max_compressed_size
        );

        if (actual_compressed_size <= 0) {
            std::cerr << "LZ4 compression failed for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << std::endl;
            region_file.close();
            return false;
        }
        compressed_buffer.resize(actual_compressed_size); // Trim to actual size

        uint32_t target_write_offset = 0;

        // Determine where to write the data
        if (current_disk_entry.isAllAir() || (current_disk_entry.offset == 0 && !current_disk_entry.isAllAir())) {
            // New slot or overwriting an 'all_air' marker. Append.
            region_file.seekg(0, std::ios::end);
            target_write_offset = static_cast<uint32_t>(region_file.tellg());
        } else if (static_cast<uint32_t>(actual_compressed_size) <= current_disk_entry.compressed_length) {
            // Fits in old spot (or smaller)
            target_write_offset = current_disk_entry.offset;
        } else {
            // Grew, append. Old spot becomes dead.
            region_file.seekg(0, std::ios::end);
            target_write_offset = static_cast<uint32_t>(region_file.tellg());
        }

        // Write the compressed data
        region_file.seekp(target_write_offset);
        region_file.write(compressed_buffer.data(), actual_compressed_size);
        if (region_file.fail()) {
            std::cerr << "Error writing chunk data to file: " << file_path_str << std::endl;
            region_file.close();
            return false;
        }

        // Update the entry to write
        entry_to_write.setAllAir(false);
        entry_to_write.offset = target_write_offset;
        entry_to_write.compressed_length = static_cast<uint32_t>(actual_compressed_size);
        entry_to_write.uncompressed_length = static_cast<uint32_t>(uncompressed_data_size);
        // entry_to_write.flags |= 0x02; // Mark as compressed if using this flag
    }

    // Write the updated index entry back to disk
    region_file.seekp(index_entry_disk_offset);
    region_file.write(reinterpret_cast<const char*>(&entry_to_write), sizeof(ChunkIndexEntry));
    if (region_file.fail()) {
        std::cerr << "Error writing updated index entry to file: " << file_path_str << std::endl;
        region_file.close();
        return false;
    }

    region_file.close();
    chunk_ref.setDirty(false); // Mark chunk as no longer dirty
    // std::cout << "Successfully saved chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " to file." << std::endl;
    return true;
}

} // namespace WorldSave