#include "RegionManager.hpp"
#include "../world/Chunk.hpp" // For CHUNK_VOLUME, CHUNK_WIDTH etc. and Chunk class
#include "../block/Blocks.hpp" // For Blocks::AIR_ID (though Chunk handles this internally)

#include <algorithm>
#include <iostream> // For error logging
#include <cstring>  // For memcpy, strcmp
#include <filesystem> // For creating directories

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp> // For glm::to_string

// Include LZ4 header
#include <lz4.h>

namespace WorldSave {

RegionManager::RegionManager() {
    // Pre-size workspace buffers to maximum possible compressed chunk size
    int uncompressed_size = static_cast<int>(CHUNK_VOLUME * sizeof(uint16_t));
    int max_compressed_size = LZ4_compressBound(uncompressed_size);
    m_compression_workspace.resize(max_compressed_size);
    m_decompression_workspace.resize(uncompressed_size); // Decompression target is uncompressed size
}

RegionManager::~RegionManager() {
    std::cout << "[RegionManager] Destructor started." << std::endl;

    // Process any remaining compaction tasks in the queue
    std::cout << "[RegionManager] Destructor: Processing remaining compaction queue..." << std::endl;
    while (processCompactionQueue(100)); // Process up to 100 at a time until queue is empty
    std::cout << "[RegionManager] Destructor: Compaction queue processed." << std::endl;

    // Now close any remaining files
    closeAllOpenRegionFiles();
    std::cout << "[RegionManager] Destructor finished." << std::endl;
}

void RegionManager::setBasePath(const std::string& world_save_path) {
    std::cout << "[RegionManager] Setting new base path to: " << world_save_path << std::endl;

    // 1. Close all currently open region files from the previous world
    closeAllOpenRegionFiles();

    // 2. Set the new base path
    m_base_save_path = world_save_path;

    // 3. Ensure the new region subdirectory exists
    std::filesystem::path region_dir_path = std::filesystem::path(*m_base_save_path) / m_subdirectory_path;
    try {
        // create_directories does nothing and returns false if the path already exists.
        // It only returns true if it created a directory. We don't need to check the return value.
        std::filesystem::create_directories(region_dir_path);
    } catch (const std::filesystem::filesystem_error& e) {
        // Throw an exception only on a true filesystem error.
        throw std::runtime_error("Filesystem error creating region directory: " + std::string(e.what()));
    }
}

std::string RegionManager::getRegionFilePath(const glm::ivec3& region_coord) const {
    if (!m_base_save_path) return ""; // Return empty if no path is set
    std::filesystem::path file_path = std::filesystem::path(*m_base_save_path) / m_subdirectory_path /
                                      ("r." + std::to_string(region_coord.x) + "." + std::to_string(region_coord.y) + "." + std::to_string(region_coord.z) + ".dat");
    return file_path.string();
}

size_t RegionManager::getLocalChunkIndex(const glm::ivec3& local_chunk_coord_in_region) const {
    // Assuming local_chunk_coord_in_region components are already modulo REGION_DIM_IN_CHUNKS
    return static_cast<size_t>(local_chunk_coord_in_region.x + // X varies fastest
                               local_chunk_coord_in_region.y * REGION_WIDTH_IN_CHUNKS +
                               local_chunk_coord_in_region.z * REGION_WIDTH_IN_CHUNKS * REGION_HEIGHT_IN_CHUNKS);
}

void RegionManager::compactRegionFile(const glm::ivec3& region_coord) {
    std::string original_file_path_str = getRegionFilePath(region_coord);
    std::fstream* original_stream_ptr = nullptr;

    // Check if the file is open in our cache and remove it if it is
    {
        std::lock_guard<std::mutex> lock(m_openFilesMutex);
        auto it = m_openRegionFiles.find(region_coord);
        if (it == m_openRegionFiles.end() || !it->second || !it->second->is_open()) {
            // std::cout << "[RegionManager] compactRegionFile: File " << original_file_path_str << " not open or not in cache. Skipping compaction." << std::endl;
            return; // File not open in cache, nothing to do here based on assumption.
        }
        original_stream_ptr = it->second.get();
        original_stream_ptr->clear(); // Clear error flags
    }
    // Note: m_openFilesMutex is unlocked here. We have the raw pointer.
    // The stream is *not* removed from the cache yet. It will be removed *after* successful compaction
    // and renaming, to avoid issues if another thread tries to open it during compaction.

    // 1. Read header and all index entries from the original file
    original_stream_ptr->seekg(0);
    RegionFileHeader header;
    original_stream_ptr->read(reinterpret_cast<char*>(&header), sizeof(RegionFileHeader));
    if (original_stream_ptr->gcount() != sizeof(RegionFileHeader) || std::strncmp(header.magic, REGION_FILE_MAGIC, 4) != 0) {
        std::cerr << "[RegionManager] compactRegionFile: Invalid header in " << original_file_path_str << ". Aborting compaction." << std::endl;
        return;
    }

    // Read the entire index table into memory
    std::vector<ChunkIndexEntry> original_index_table(REGION_VOLUME_IN_CHUNKS);
    original_stream_ptr->read(reinterpret_cast<char*>(original_index_table.data()), original_index_table.size() * sizeof(ChunkIndexEntry));
    if (original_stream_ptr->gcount() != static_cast<std::streamsize>(original_index_table.size() * sizeof(ChunkIndexEntry))) {
        std::cerr << "[RegionManager] compactRegionFile: Failed to read full index table from " << original_file_path_str << ". Aborting." << std::endl;
        return;
    }

    // 2. Determine if compaction is needed
    struct ValidChunkInfo {
        uint32_t offset;
        uint32_t length;
        uint32_t uncompressed_length;
        size_t original_chunk_idx_in_region; // To map back to the new index table
    };
    std::vector<ValidChunkInfo> valid_chunks_info;
    for (size_t i = 0; i < original_index_table.size(); ++i) {
        const auto& entry = original_index_table[i];
        if (!entry.isAllAir() && entry.offset > 0 && entry.compressed_length > 0) {
            valid_chunks_info.push_back({entry.offset, entry.compressed_length, entry.uncompressed_length, i});
        }
    }

    if (valid_chunks_info.empty()) { // No actual chunk data, only all_air or empty entries
        original_stream_ptr->seekg(0, std::ios::end);
        uint32_t actual_file_size = static_cast<uint32_t>(original_stream_ptr->tellg());
        uint32_t min_file_size = sizeof(RegionFileHeader) + REGION_VOLUME_IN_CHUNKS * sizeof(ChunkIndexEntry);
        if (actual_file_size <= min_file_size) { // Already minimal or empty
            // std::cout << "[RegionManager] compactRegionFile: " << original_file_path_str << " is empty or minimal. No compaction needed." << std::endl;
            return;
        }
        // Else, file is larger than minimal, needs truncation (compaction will achieve this)
    } else {
        std::sort(valid_chunks_info.begin(), valid_chunks_info.end(), [](const ValidChunkInfo& a, const ValidChunkInfo& b){
            return a.offset < b.offset;
        });

        bool needs_compaction_flag = false;
        uint32_t expected_next_data_offset = sizeof(RegionFileHeader) + REGION_VOLUME_IN_CHUNKS * sizeof(ChunkIndexEntry);
        for (const auto& info : valid_chunks_info) {
            if (info.offset != expected_next_data_offset) {
                needs_compaction_flag = true;
                break;
            }
            expected_next_data_offset += info.length;
        }

        if (!needs_compaction_flag) {
            original_stream_ptr->seekg(0, std::ios::end);
            uint32_t actual_file_size = static_cast<uint32_t>(original_stream_ptr->tellg());
            if (actual_file_size == expected_next_data_offset) {
                // std::cout << "[RegionManager] compactRegionFile: " << original_file_path_str << " is already optimally packed. No compaction needed." << std::endl;
                return;
            }
        }
    }
    std::cout << "[RegionManager] Compaction needed for " << original_file_path_str << std::endl;

    // 3. Perform Compaction
    std::string temp_file_path_str = original_file_path_str + ".tmp";
    std::fstream temp_stream(temp_file_path_str, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!temp_stream.is_open()) {
        std::cerr << "[RegionManager] compactRegionFile: Failed to open temporary file " << temp_file_path_str << ". Aborting." << std::endl;
        return;
    }

    // Write header to temp file
    temp_stream.write(reinterpret_cast<const char*>(&header), sizeof(RegionFileHeader));

    std::vector<ChunkIndexEntry> new_index_table(REGION_VOLUME_IN_CHUNKS); // Zero-initialized
    uint32_t current_data_offset_in_temp = sizeof(RegionFileHeader) + REGION_VOLUME_IN_CHUNKS * sizeof(ChunkIndexEntry);

    // Iterate through sorted valid chunks and write them to the temp file
    for (const auto& info : valid_chunks_info) {
        std::vector<char> chunk_data_buffer(info.length);
        original_stream_ptr->seekg(info.offset);
        original_stream_ptr->read(chunk_data_buffer.data(), info.length);
        if (original_stream_ptr->gcount() != static_cast<std::streamsize>(info.length)) {
            std::cerr << "[RegionManager] compactRegionFile: Failed to read chunk data from " << original_file_path_str << ". Aborting." << std::endl;
            temp_stream.close();
            std::filesystem::remove(temp_file_path_str);
            return;
        }

        temp_stream.seekp(current_data_offset_in_temp);
        temp_stream.write(chunk_data_buffer.data(), info.length);

        new_index_table[info.original_chunk_idx_in_region].offset = current_data_offset_in_temp;
        new_index_table[info.original_chunk_idx_in_region].compressed_length = info.length;
        new_index_table[info.original_chunk_idx_in_region].uncompressed_length = info.uncompressed_length;
        new_index_table[info.original_chunk_idx_in_region].setAllAir(false);

        current_data_offset_in_temp += info.length;
    }

    // Fill in "all_air" entries in the new index table
    for (size_t i = 0; i < original_index_table.size(); ++i) {
        if (original_index_table[i].isAllAir()) {
            new_index_table[i].setAllAir(true); // offset, lengths remain 0
        }
    }

    // Write new index table to temp file
    temp_stream.seekp(sizeof(RegionFileHeader));
    temp_stream.write(reinterpret_cast<const char*>(new_index_table.data()), new_index_table.size() * sizeof(ChunkIndexEntry));
    temp_stream.close();

    // 4. Replace original file with temp file
    // Close the original stream and remove it from our cache *before* renaming
    {
        std::lock_guard<std::mutex> lock(m_openFilesMutex);
        auto it = m_openRegionFiles.find(region_coord);
        if (it != m_openRegionFiles.end()) {
            if (it->second && it->second->is_open()) it->second->close();
            m_openRegionFiles.erase(it);
            // std::cout << "[RegionManager] Closed and removed stream for " << original_file_path_str << " from cache before rename." << std::endl;
        }
    }

    try {
        std::filesystem::remove(original_file_path_str);
        std::filesystem::rename(temp_file_path_str, original_file_path_str);
        std::cout << "[RegionManager] Successfully compacted " << original_file_path_str << std::endl;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[RegionManager] Filesystem error during compaction of " << original_file_path_str << ": " << e.what() << std::endl;
        // Attempt to remove temp file if rename failed
        if (std::filesystem::exists(temp_file_path_str)) std::filesystem::remove(temp_file_path_str);
    }
}

bool RegionManager::processCompactionQueue(int max_to_process) {
    glm::ivec3 region_coord_to_compact;
    bool found_task = false;

    { // Scope for m_countersMutex
        std::lock_guard<std::mutex> lock(m_countersMutex);
        if (!m_regionsToCompactQueue.empty() && max_to_process > 0) {
            region_coord_to_compact = m_regionsToCompactQueue.front();
            m_regionsToCompactQueue.pop();
            found_task = true;
        }
    } // m_countersMutex is released

    if (found_task) {
        compactRegionFile(region_coord_to_compact);
    }
    return found_task; // Return true if a task was processed
}

std::fstream* RegionManager::getRegionFileStream(const glm::ivec3& region_coord) {
    std::lock_guard<std::mutex> lock(m_openFilesMutex);

    auto it = m_openRegionFiles.find(region_coord);
    if (it != m_openRegionFiles.end()) {
        // File is in cache, check if stream is good
        if (it->second && it->second->is_open()) {
            it->second->clear(); // Clear any error flags (like EOF) before reuse
            return it->second.get();
        } else {
            // Stream was in map but closed or in an error state, remove to reopen
            m_openRegionFiles.erase(it);
        }
    }

    // If we are about to open a file for a region that was queued for compaction,
    // remove it from the compaction queue as it's now active again.
    {
        std::lock_guard<std::mutex> counterLock(m_countersMutex);
        // Note: Checking/removing from queue efficiently requires iterating or using a different structure (like std::set).
        // For simplicity with std::queue, we'd need to rebuild the queue, which is inefficient.
        // A better approach for the queue might be a std::set or std::map if frequent removal by key is needed.
    }

    // Stream not in cache or was invalid, try to open/create
    std::string file_path_str = getRegionFilePath(region_coord);
    auto new_stream_ptr = std::make_unique<std::fstream>();

    // Try to open for read/write first (if it exists)
    new_stream_ptr->open(file_path_str, std::ios::in | std::ios::out | std::ios::binary);

    if (!new_stream_ptr->is_open()) {
        // File doesn't exist (or other open error), try to create it
        new_stream_ptr->open(file_path_str, std::ios::out | std::ios::binary | std::ios::trunc); // Create/truncate
        if (!new_stream_ptr->is_open()) {
            std::cerr << "[RegionManager] Error: Could not create region file for writing: " << file_path_str << std::endl;
            return nullptr;
        }
        // std::cout << "[RegionManager] New region file created: " << file_path_str << std::endl;

        // Write header
        RegionFileHeader new_header;
        std::memcpy(new_header.magic, REGION_FILE_MAGIC, sizeof(REGION_FILE_MAGIC));
        new_header.version = REGION_FILE_VERSION;
        new_header.region_dim_x_chunks = REGION_WIDTH_IN_CHUNKS;
        new_header.region_dim_y_chunks = REGION_HEIGHT_IN_CHUNKS;
        new_header.region_dim_z_chunks = REGION_DEPTH_IN_CHUNKS;
        std::memset(new_header.reserved, 0, sizeof(new_header.reserved));
        new_stream_ptr->write(reinterpret_cast<const char*>(&new_header), sizeof(RegionFileHeader));

        // Write empty index table
        std::vector<ChunkIndexEntry> empty_index_table(REGION_VOLUME_IN_CHUNKS);
        new_stream_ptr->write(reinterpret_cast<const char*>(empty_index_table.data()), empty_index_table.size() * sizeof(ChunkIndexEntry));

        if (new_stream_ptr->fail()) {
            std::cerr << "[RegionManager] Error initializing new region file: " << file_path_str << std::endl;
            new_stream_ptr->close();
            // Optionally delete the partially written file: std::filesystem::remove(file_path_str);
            return nullptr;
        }
        new_stream_ptr->close(); // Close after initial write

        // Reopen in r/w mode
        new_stream_ptr->open(file_path_str, std::ios::in | std::ios::out | std::ios::binary);
        if (!new_stream_ptr->is_open()) {
            std::cerr << "[RegionManager] Error: Could not reopen new region file for r/w: " << file_path_str << std::endl;
            return nullptr;
        }
    }
    // If we reach here, the file (either existing or newly created) is open in new_stream_ptr
    // For existing files, header validation will happen in load/save if needed, or could be added here.
    // For simplicity, load/save will re-validate if they are the first to use an existing stream.
    auto [inserted_it, success] = m_openRegionFiles.emplace(region_coord, std::move(new_stream_ptr));
    return inserted_it->second.get();
}

bool RegionManager::loadChunkFromFile(Chunk& chunk_ref) {
    if (!m_base_save_path.has_value()) {
        std::cerr << "[RegionManager] Error: loadChunkFromFile called but no base path is set." << std::endl;
        return false;
    }

    glm::ivec3 chunk_coord = chunk_ref.getChunkCoord();

    // Calculate region coordinates using integer arithmetic for floored division
    glm::ivec3 region_coord;
    region_coord.x = chunk_coord.x / REGION_WIDTH_IN_CHUNKS;
    region_coord.y = chunk_coord.y / REGION_HEIGHT_IN_CHUNKS;
    region_coord.z = chunk_coord.z / REGION_DEPTH_IN_CHUNKS;

    if (chunk_coord.x < 0 && (chunk_coord.x % REGION_WIDTH_IN_CHUNKS != 0)) {
        region_coord.x--;
    }
    if (chunk_coord.y < 0 && (chunk_coord.y % REGION_HEIGHT_IN_CHUNKS != 0)) {
        region_coord.y--;
    }
    if (chunk_coord.z < 0 && (chunk_coord.z % REGION_DEPTH_IN_CHUNKS != 0)) {
        region_coord.z--;
    }

    // Calculate local chunk coordinates within the region
    // This calculation remains correct if region_coord is the floored division result.
    glm::ivec3 local_chunk_coord(
        chunk_coord.x - region_coord.x * REGION_WIDTH_IN_CHUNKS,
        chunk_coord.y - region_coord.y * REGION_HEIGHT_IN_CHUNKS,
        chunk_coord.z - region_coord.z * REGION_DEPTH_IN_CHUNKS
    );

    std::fstream* p_region_file = getRegionFileStream(region_coord);
    if (!p_region_file || !p_region_file->is_open()) {
        // std::cout << "Region file not found (or cannot be opened): " << file_path_str << " for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << std::endl;
        chunk_ref.setWasLoadedFromFile(false); // Ensure flag is false if not loaded
        return false; // File doesn't exist or couldn't be opened, so chunk data isn't saved here.
    }
    std::fstream& region_file = *p_region_file;
    std::string file_path_str = getRegionFilePath(region_coord); // For logging

    // 1. Read and validate RegionFileHeader
    RegionFileHeader header;
    region_file.seekg(0); // Ensure we read header from the beginning
    region_file.read(reinterpret_cast<char*>(&header), sizeof(RegionFileHeader));
    if (region_file.gcount() != sizeof(RegionFileHeader)) {
        std::cerr << "[RegionManager] Error reading region file header from: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    if (std::strncmp(header.magic, REGION_FILE_MAGIC, 4) != 0 || header.version != REGION_FILE_VERSION) {
        std::cerr << "Region file header mismatch (magic/version) in: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }
    if (header.region_dim_x_chunks != REGION_WIDTH_IN_CHUNKS ||
        header.region_dim_y_chunks != REGION_HEIGHT_IN_CHUNKS ||
        header.region_dim_z_chunks != REGION_DEPTH_IN_CHUNKS) {
        std::cerr << "[RegionManager] Region file dimension mismatch in: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    // 2. Calculate local index and seek to ChunkIndexEntry
    size_t chunk_idx_in_region = getLocalChunkIndex(local_chunk_coord);
    size_t index_table_offset = sizeof(RegionFileHeader);
    size_t target_entry_offset = index_table_offset + (chunk_idx_in_region * sizeof(ChunkIndexEntry));

    region_file.seekg(target_entry_offset);
    if (region_file.fail()) {
        std::cerr << "[RegionManager] Error seeking to chunk index entry in: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    // 3. Read ChunkIndexEntry
    ChunkIndexEntry entry;
    region_file.read(reinterpret_cast<char*>(&entry), sizeof(ChunkIndexEntry));
    if (region_file.gcount() != sizeof(ChunkIndexEntry)) {
        std::cerr << "[RegionManager] Error reading chunk index entry from: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    // 4. Process based on ChunkIndexEntry
    if (entry.isAllAir()) {
        chunk_ref.setAllAir(); // This also sets dirty to false
        chunk_ref.markGenerated(); // Mark as "loaded"
        chunk_ref.setWasLoadedFromFile(true);
        {
            std::lock_guard<std::mutex> lock(m_countersMutex);
            m_activeChunkCounters[region_coord]++;
            // std::cout << "[RegionManager] Incremented active count for region " << region_coord.x << "," << region_coord.y << "," << region_coord.z << " to " << m_activeChunkCounters[region_coord] << " (all_air load)" << std::endl;
        }
        // std::cout << "Loaded chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " as all_air from file." << std::endl;
        return true;
    }

    // If not allAir, but offset or length is 0, it means it's not actually saved (or an error in saving)
    if (entry.offset == 0 || entry.compressed_length == 0 || entry.uncompressed_length == 0) {
         //std::cout << "Chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " entry in region file indicates no data saved." << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false; // Chunk data not present in the file
    }

    // Check if expected uncompressed length matches our chunk volume
    if (entry.uncompressed_length != CHUNK_VOLUME * sizeof(uint16_t)) {
        std::cerr << "[RegionManager] Mismatch in uncompressed length for chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z
                  << " in file " << file_path_str
                  << ". Expected: " << CHUNK_VOLUME * sizeof(uint16_t) 
                  << ", Got: " << entry.uncompressed_length << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    // 5. Read compressed data
    {
        std::lock_guard<std::mutex> lock(m_workspace_mutex);
        m_decompression_workspace.resize(entry.compressed_length); // Resize shared buffer
    }
    region_file.seekg(entry.offset); // entry.offset is from the start of the file
    if (region_file.fail()) {
        std::cerr << "[RegionManager] Error seeking to chunk data in: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }
    region_file.read(m_decompression_workspace.data(), entry.compressed_length); // Read into the shared workspace buffer
    if (region_file.gcount() != static_cast<std::streamsize>(entry.compressed_length)) {
        std::cerr << "[RegionManager] Error reading compressed chunk data from: " << file_path_str << std::endl;
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    // 6. Decompress data directly into chunk memory using performLockedWrite
    bool decompression_successful = false;

    chunk_ref.performLockedWrite(
        [&](uint16_t* chunk_block_ptr, size_t buffer_capacity_elements) {
            // This lambda executes while Chunk's m_data_mutex is locked.
            if (buffer_capacity_elements * sizeof(uint16_t) < entry.uncompressed_length) {
                std::cerr << "[RegionManager] Error: Chunk buffer capacity ("
                          << buffer_capacity_elements * sizeof(uint16_t)
                          << " bytes) too small for uncompressed data ("
                          << entry.uncompressed_length << " bytes) for chunk " << glm::to_string(chunk_coord) << std::endl;
                return; // decompression_successful remains false
            }

            char* target_char_ptr = reinterpret_cast<char*>(chunk_block_ptr);
            int decompressed_size_actual = LZ4_decompress_safe(
                m_decompression_workspace.data(), // Use data from the shared workspace buffer
                target_char_ptr,
                static_cast<int>(entry.compressed_length),
                static_cast<int>(entry.uncompressed_length) // Max size to write
            );

            if (decompressed_size_actual < 0) {
                std::cerr << "[RegionManager] LZ4 decompression failed for chunk " << glm::to_string(chunk_coord)
                          << " in file " << file_path_str << ". Error code: " << decompressed_size_actual << std::endl;
                return; // decompression_successful remains false
            }
            if (static_cast<uint32_t>(decompressed_size_actual) != entry.uncompressed_length) {
                std::cerr << "[RegionManager] LZ4 decompression size mismatch for chunk " << glm::to_string(chunk_coord)
                          << ". Expected: " << entry.uncompressed_length << ", Got: " << decompressed_size_actual << std::endl;
                return; // decompression_successful remains false
            }
            
            decompression_successful = true; // Mark as successful
        }
    );

    if (!decompression_successful) {
        // Decompression failed or prerequisites not met within the lambda.
        chunk_ref.setWasLoadedFromFile(false);
        return false;
    }

    // Decompression was successful, finalize chunk state
    chunk_ref.markGenerated(); // Mark as "loaded"
    chunk_ref.setWasLoadedFromFile(true);
    chunk_ref.setDirty(false); // Data loaded from file is not initially dirty
    {
        std::lock_guard<std::mutex> lock(m_countersMutex);
        m_activeChunkCounters[region_coord]++;
        // std::cout << "[RegionManager] Incremented active count for region " << glm::to_string(region_coord) << " to " << m_activeChunkCounters[region_coord] << " (data load)" << std::endl;
    }
    // std::cout << "Successfully loaded and decompressed chunk " << glm::to_string(chunk_coord) << " directly into chunk memory." << std::endl;
    return true;
}

bool RegionManager::saveChunkToFile(Chunk& chunk_ref) {
    if (!m_base_save_path.has_value()) {
        std::cerr << "[RegionManager] Error: saveChunkToFile called but no base path is set." << std::endl;
        return false;
    }

    glm::ivec3 chunk_coord = chunk_ref.getChunkCoord();

    // Calculate region coordinates using integer arithmetic for floored division
    glm::ivec3 region_coord;
    region_coord.x = chunk_coord.x / REGION_WIDTH_IN_CHUNKS;
    region_coord.y = chunk_coord.y / REGION_HEIGHT_IN_CHUNKS;
    region_coord.z = chunk_coord.z / REGION_DEPTH_IN_CHUNKS;

    if (chunk_coord.x < 0 && (chunk_coord.x % REGION_WIDTH_IN_CHUNKS != 0)) {
        region_coord.x--;
    }
    if (chunk_coord.y < 0 && (chunk_coord.y % REGION_HEIGHT_IN_CHUNKS != 0)) {
        region_coord.y--;
    }
    if (chunk_coord.z < 0 && (chunk_coord.z % REGION_DEPTH_IN_CHUNKS != 0)) {
        region_coord.z--;
    }

    // Calculate local chunk coordinates within the region
    glm::ivec3 local_chunk_coord(
        chunk_coord.x - region_coord.x * REGION_WIDTH_IN_CHUNKS,
        chunk_coord.y - region_coord.y * REGION_HEIGHT_IN_CHUNKS,
        chunk_coord.z - region_coord.z * REGION_DEPTH_IN_CHUNKS
    );

    std::fstream* p_region_file = getRegionFileStream(region_coord);
    if (!p_region_file || !p_region_file->is_open()) {
        std::cerr << "[RegionManager] Error: Could not get/open region file stream for saving chunk "
                  << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << std::endl;
        return false;
    }
    std::fstream& region_file = *p_region_file;
    std::string file_path_str = getRegionFilePath(region_coord); // For logging

    // Validate header of existing file if it was just opened by getRegionFileStream
    // This is a bit redundant if getRegionFileStream already did it, but ensures consistency.
    // A more robust getRegionFileStream would pass back a "newly_created" flag or similar.
    // For now, let's assume getRegionFileStream ensures it's a valid region file.
    {
        region_file.seekg(0);
        RegionFileHeader header_check;
        region_file.read(reinterpret_cast<char*>(&header_check), sizeof(RegionFileHeader));
        if (region_file.gcount() != sizeof(RegionFileHeader) ||
            std::strncmp(header_check.magic, REGION_FILE_MAGIC, 4) != 0 ||
            header_check.version != REGION_FILE_VERSION) {
            std::cerr << "[RegionManager] Region file header mismatch on existing file (save): " << file_path_str << std::endl;
            // Don't close, it's managed by the cache. Mark as error and return.
            return false;
        }
    }

    // Get the index entry for this chunk
    size_t chunk_idx_in_region = getLocalChunkIndex(local_chunk_coord);
    size_t index_entry_disk_offset = sizeof(RegionFileHeader) + (chunk_idx_in_region * sizeof(ChunkIndexEntry));

    ChunkIndexEntry current_disk_entry;
    region_file.seekg(index_entry_disk_offset);
    if (region_file.fail()) {
         std::cerr << "[RegionManager] Error seeking to index entry for save: " << file_path_str << std::endl;
         return false;
    }
    region_file.read(reinterpret_cast<char*>(&current_disk_entry), sizeof(ChunkIndexEntry));
    if (region_file.gcount() != sizeof(ChunkIndexEntry)) {
        std::cerr << "[RegionManager] Error reading index entry for save: " << file_path_str << std::endl;
        return false;
    }

    if (chunk_ref.isAllAir()) {
        ChunkIndexEntry entry_to_write; // Create a new, clean entry
        entry_to_write.setAllAir(true);
        entry_to_write.offset = 0;
        entry_to_write.compressed_length = 0;
        entry_to_write.uncompressed_length = 0;

        // Write the updated index entry back to disk
        region_file.seekp(index_entry_disk_offset);
        region_file.write(reinterpret_cast<const char*>(&entry_to_write), sizeof(ChunkIndexEntry));
        if (region_file.fail()) {
            std::cerr << "[RegionManager] Error writing updated index entry (all_air) to file: " << file_path_str << std::endl;
            return false;
        }
    } else {
        // Use a locked read method to avoid copying the chunk data
        bool save_successful = false;
        chunk_ref.performLockedRead(
            [&](const uint16_t* chunk_block_ptr, size_t buffer_capacity_elements) {
            // This lambda executes while Chunk's m_data_mutex is locked.
            
            if (!chunk_block_ptr) { // Should not happen if !isAllAir(), but good to check.
                 std::cerr << "[RegionManager] Error: Chunk " << glm::to_string(chunk_coord) << " is not all_air but performLockedRead provided null block data." << std::endl;
                 return; // save_successful remains false
            }

            const char* uncompressed_data_ptr = reinterpret_cast<const char*>(chunk_block_ptr);
            int uncompressed_data_size = static_cast<int>(CHUNK_VOLUME * sizeof(uint16_t)); // Use defined CHUNK_VOLUME for expected size

            if (uncompressed_data_size != CHUNK_VOLUME * sizeof(uint16_t)) {
                 std::cerr << "[RegionManager] Error: Chunk buffer size mismatch for chunk " << glm::to_string(chunk_coord) << std::endl;
                 return; // save_successful remains false
            }

            int max_compressed_size = LZ4_compressBound(uncompressed_data_size);
            {
                std::lock_guard<std::mutex> lock(m_workspace_mutex);
                m_compression_workspace.resize(max_compressed_size); // Resize shared buffer
            }

            int actual_compressed_size = LZ4_compress_default(
                uncompressed_data_ptr,
                m_compression_workspace.data(), // Compress into shared workspace buffer
                uncompressed_data_size,
                max_compressed_size
            );

            if (actual_compressed_size <= 0) {
                std::cerr << "[RegionManager] LZ4 compression failed for chunk " << glm::to_string(chunk_coord) << std::endl;
                return; // save_successful remains false
            }

            uint32_t target_write_offset = 0;

            // Determine where to write the data
            if (current_disk_entry.isAllAir() || (current_disk_entry.offset == 0 && !current_disk_entry.isAllAir())) {
                // New slot or overwriting an 'all_air' marker. Append.
                region_file.seekg(0, std::ios::end); // Use seekg to find end position
                target_write_offset = static_cast<uint32_t>(region_file.tellg()); // Use tellg for position
            } else if (static_cast<uint32_t>(actual_compressed_size) <= current_disk_entry.compressed_length) {
                // Fits in old spot (or smaller)
                target_write_offset = current_disk_entry.offset;
            } else {
                // Grew, append. Old spot becomes dead.
                region_file.seekg(0, std::ios::end); // Use seekg to find end position
                target_write_offset = static_cast<uint32_t>(region_file.tellg()); // Use tellg for position
            }

            // Write the compressed data
            region_file.seekp(target_write_offset);
            region_file.write(m_compression_workspace.data(), actual_compressed_size); // Write from shared workspace buffer
            if (region_file.fail()) {
                std::cerr << "[RegionManager] Error writing chunk data to file: " << file_path_str << std::endl;
                return; // save_successful remains false
            }

            // Update the entry to write
            ChunkIndexEntry entry_to_write; // Create a new entry for writing
            entry_to_write.setAllAir(false);
            entry_to_write.offset = target_write_offset;
            entry_to_write.compressed_length = static_cast<uint32_t>(actual_compressed_size);
            entry_to_write.uncompressed_length = static_cast<uint32_t>(uncompressed_data_size);
            // entry_to_write.flags |= 0x02; // Mark as compressed if using this flag

            // Write the updated index entry back to disk
            region_file.seekp(index_entry_disk_offset);
            region_file.write(reinterpret_cast<const char*>(&entry_to_write), sizeof(ChunkIndexEntry));
            if (region_file.fail()) {
                std::cerr << "[RegionManager] Error writing updated index entry to file: " << file_path_str << std::endl;
                return; // save_successful remains false
            }

            save_successful = true; // Mark as successful
        }); // Chunk's m_data_mutex is released here

        if (!save_successful) {
            std::cerr << "[RegionManager] Error: Chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z
                      << " failed during locked read/compression/write." << std::endl;
            return false;
        }
    }

    region_file.flush(); // Ensure data is written from stream buffers
    chunk_ref.setDirty(false); // Mark chunk as no longer dirty
    // Do NOT close region_file here, it's managed by the cache.
    // std::cout << "Successfully saved chunk " << chunk_coord.x << "," << chunk_coord.y << "," << chunk_coord.z << " to file." << std::endl;
    return true;
}

void RegionManager::notifyChunkUnloaded(const Chunk& unloadedChunk) {
    if (!unloadedChunk.wasLoadedFromFile()) {
        // This chunk was not loaded from a file (e.g., it was newly generated and possibly saved, but its current instance in memory didn't originate from a load operation).
        // Per requirements, do not decrement the counter.
        // std::cout << "[RegionManager] notifyChunkUnloaded: Chunk " << glm::to_string(unloadedChunk.getChunkCoord()) << " was not loaded from file. No counter change." << std::endl;
        return;
    }

    glm::ivec3 chunk_coord = unloadedChunk.getChunkCoord();
    glm::ivec3 region_coord; // Calculate region_coord as before
    region_coord.x = chunk_coord.x / REGION_WIDTH_IN_CHUNKS;
    region_coord.y = chunk_coord.y / REGION_HEIGHT_IN_CHUNKS;
    region_coord.z = chunk_coord.z / REGION_DEPTH_IN_CHUNKS;
    if (chunk_coord.x < 0 && (chunk_coord.x % REGION_WIDTH_IN_CHUNKS != 0)) region_coord.x--;
    if (chunk_coord.y < 0 && (chunk_coord.y % REGION_HEIGHT_IN_CHUNKS != 0)) region_coord.y--;
    if (chunk_coord.z < 0 && (chunk_coord.z % REGION_DEPTH_IN_CHUNKS != 0)) region_coord.z--;

    bool region_became_empty = false;
    { // Scope for m_countersMutex (also protects m_regionsToCompactQueue)
        std::lock_guard<std::mutex> lock(m_countersMutex);
        auto it = m_activeChunkCounters.find(region_coord);
        if (it != m_activeChunkCounters.end()) {
            if (it->second > 0) {
                it->second--;
                // std::cout << "[RegionManager] Decremented active count for region " << glm::to_string(region_coord) << " to " << it->second << std::endl;
                if (it->second == 0) {
                    std::cout << "[RegionManager] Region " << glm::to_string(region_coord) << " active chunk count reached zero. Queueing for compaction." << std::endl;
                    m_activeChunkCounters.erase(it); // Erase the counter entry for this region *immediately*
                    m_regionsToCompactQueue.push(region_coord); // Add to compaction queue
                    region_became_empty = true;
                }
            } else {
                 std::cerr << "[RegionManager] Warning: Active chunk counter for region " << glm::to_string(region_coord) << " was already zero or negative before decrementing." << std::endl;
            }
        } else {
            std::cerr << "[RegionManager] Warning: No active chunk counter found for region " << glm::to_string(region_coord) << " during unload of a file-loaded chunk." << std::endl;
        }
    } // m_countersMutex is released
}

void RegionManager::closeAllOpenRegionFiles() {
    std::lock_guard<std::mutex> lock(m_openFilesMutex);
    for (auto& pair : m_openRegionFiles) {
        if (pair.second && pair.second->is_open()) {
            pair.second->close();
        }
    }
    m_openRegionFiles.clear();
    std::cout << "[RegionManager] All open region files have been closed and cache cleared." << std::endl;
}

} // namespace WorldSave