#include "SaveGameManager.hpp"
#include "../resource/json/WorldMetadata.hpp"
#include "../util/DebugLog.hpp"

#include <filesystem> // For directory operations
#include <optional>
#include <chrono>     // For timestamps
#include <random>     // For random seed generation
#include <regex>      // For sanitizing world name
#include <algorithm>  // For std::sort
#include <string>
#include <iostream>

/******** PUBLIC MEMBER FUNCTIONS ********/

SaveGameManager::SaveGameManager(const std::string& savesPath) : m_savesPath(savesPath) {
    // In the next step, we will implement the logic here to:
    // 1. Ensure the m_savesPath directory exists.
    try {
        if (!std::filesystem::exists(m_savesPath)) {
            std::cout << "[SaveGameManager] Saves directory not found. Creating at: " << m_savesPath << std::endl;
            std::filesystem::create_directories(m_savesPath);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Error creating saves directory: " + std::string(e.what()));
    }

    // 2. Scan for subdirectories.
    // 3. Use WorldSave::loadMetadata to populate m_worlds.
    for (const auto& entry : std::filesystem::directory_iterator(m_savesPath)) {
        if (entry.is_directory()) {
            // 3. Check for a metadata file in each subdirectory.
            const std::filesystem::path metadataPath = entry.path() / "world.meta";

            if (std::filesystem::exists(metadataPath)) {
                // 4. Use WorldSave::loadMetadata to populate m_worlds.
                if (auto metadata = WorldSave::loadMetadata(metadataPath.string())) {
                    m_worlds.push_back(*metadata);
                }
            }
        }
    }

    // Sort the worlds by last played timestamp in descending order (most recent first).
    std::sort(m_worlds.begin(), m_worlds.end(), [](const WorldMetadata& a, const WorldMetadata& b) {
        return a.lastPlayedTimestamp > b.lastPlayedTimestamp;
    });

    std::cout << "[SaveGameManager] Discovered " << m_worlds.size() << " saved world(s)." << std::endl;
    for (const auto& world : m_worlds) {
        std::cout << "  - Found world: '" << world.worldName << "' in directory '" << world.directoryName << "'" << std::endl;
    }
}

WorldMetadata SaveGameManager::createNewWorld(const std::string& worldName, const std::string& generatorId, std::optional<int64_t> seed) {
    // 1. Generate timestamps and seed
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    int64_t worldSeed = seed.value_or(static_cast<int64_t>(timestamp));
    if (!seed.has_value()) {
        // If no seed was provided, use a better random source than just the timestamp
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<int64_t> distrib;
        worldSeed = distrib(gen);
    }

    // 2. Generate a unique, safe directory name
    std::string sanitizedName = this->sanitizeNameForDirectory(worldName);
    std::string directoryName = sanitizedName + "_" + std::to_string(timestamp);
    std::filesystem::path worldPath = std::filesystem::path(m_savesPath) / directoryName;

    // 3. Create the world directory
    try {
        std::filesystem::create_directory(worldPath);
        // As per our design decision, we will NOT create subdirectories like 'regions' here.
        // The components that need them (like RegionManager) will create them.
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Failed to create world directory: " + std::string(e.what()));
    }

    // 4. Populate the metadata object
    WorldMetadata newWorldMeta;
    newWorldMeta.worldName = worldName;
    newWorldMeta.directoryName = directoryName;
    newWorldMeta.generatorId = generatorId;
    newWorldMeta.seed = worldSeed;
    newWorldMeta.creationTimestamp = timestamp;
    newWorldMeta.lastPlayedTimestamp = timestamp; // It's being played right now

    // 5. Save the metadata to world.meta
    std::filesystem::path metadataPath = worldPath / "world.meta";
    WorldSave::saveMetadata(newWorldMeta, metadataPath.string());

    // 6. Add to internal list and re-sort
    m_worlds.push_back(newWorldMeta);
    std::sort(m_worlds.begin(), m_worlds.end(), [](const WorldMetadata& a, const WorldMetadata& b) {
        return a.lastPlayedTimestamp > b.lastPlayedTimestamp;
    });

    std::cout << "[SaveGameManager] Created new world '" << newWorldMeta.worldName << "' with seed " << newWorldMeta.seed << std::endl;

    // 7. Return the new metadata
    return newWorldMeta;
}

void SaveGameManager::deleteWorld(const std::string& directoryName) {
    // 1. Find and remove the world from the internal list using the erase-remove idiom.
    auto it = std::remove_if(m_worlds.begin(), m_worlds.end(),
        [&directoryName](const WorldMetadata& meta) {
            return meta.directoryName == directoryName;
        });

    if (it == m_worlds.end()) {
        std::cerr << "[SaveGameManager] Warning: Attempted to delete a world that was not found: " << directoryName << std::endl;
        return; // World not found in our list.
    }
    m_worlds.erase(it, m_worlds.end());

    // 2. Construct the full path to the world directory.
    std::filesystem::path worldPath = std::filesystem::path(m_savesPath) / directoryName;

    // 3. Recursively delete the directory from the filesystem.
    try {
        if (std::filesystem::exists(worldPath)) {
            std::filesystem::remove_all(worldPath);
            std::cout << "[SaveGameManager] Deleted world directory: " << worldPath << std::endl;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // This is a more serious error, but we've already removed it from the list.
        // The user will see it as deleted, but files may remain.
        std::cerr << "[SaveGameManager] Error deleting world directory " << worldPath << ": " << e.what() << std::endl;
    }
}

void SaveGameManager::updateLastPlayed(const std::string& directoryName) {
    // 1. Find the world in our internal list.
    auto it = std::find_if(m_worlds.begin(), m_worlds.end(),
        [&directoryName](const WorldMetadata& meta) {
            return meta.directoryName == directoryName;
        });

    if (it == m_worlds.end()) {
        std::cerr << "[SaveGameManager] Warning: Tried to update last played time for a world not in the list: " << directoryName << std::endl;
        return;
    }

    // 2. Update the timestamp and re-save the metadata file.
    const auto now = std::chrono::system_clock::now();
    it->lastPlayedTimestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::filesystem::path metadataPath = std::filesystem::path(m_savesPath) / it->directoryName / "world.meta";
    WorldSave::saveMetadata(*it, metadataPath.string());

    // 3. Re-sort the list to move the updated world to the front.
    std::sort(m_worlds.begin(), m_worlds.end(), [](const WorldMetadata& a, const WorldMetadata& b) {
        return a.lastPlayedTimestamp > b.lastPlayedTimestamp;
    });
}

void SaveGameManager::renameWorld(const std::string& directoryName, const std::string& newWorldName) {
    // 1. Find the world in our internal list.
    auto it = std::find_if(m_worlds.begin(), m_worlds.end(),
        [&directoryName](const WorldMetadata& meta) {
            return meta.directoryName == directoryName;
        });

    if (it == m_worlds.end()) {
        std::cerr << "[SaveGameManager] Warning: Tried to rename a world not in the list: " << directoryName << std::endl;
        return;
    }

    // 2. Update the world name and re-save the metadata file.
    it->worldName = newWorldName;
    std::filesystem::path metadataPath = std::filesystem::path(m_savesPath) / it->directoryName / "world.meta";
    WorldSave::saveMetadata(*it, metadataPath.string());

    VK_LOG("[SaveGameManager] Renamed world in directory '" << directoryName << "' to new name '" << newWorldName << "'");
}

/******** PRIVATE MEMBER FUNCTIONS ********/

std::string SaveGameManager::sanitizeNameForDirectory(const std::string& name) {
    // Replace spaces with underscores
    std::string sanitized = std::regex_replace(name, std::regex("\\s+"), "_");
    // Remove all non-alphanumeric characters except underscore
    sanitized = std::regex_replace(sanitized, std::regex("[^a-zA-Z0-9_]"), "");
    // Prevent empty or dot-only names which can be problematic
    if (sanitized.empty() || sanitized.find_first_not_of('.') == std::string::npos) {
        return "invalid_name";
    }
    return sanitized;
}