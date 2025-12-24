#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include "json.hpp"

/**
 * @brief A lightweight structure to hold metadata about a saved world.
 * This data is loaded quickly to populate the world selection menu.
 */
struct WorldMetadata {
    std::string worldName;       // User-facing name, e.g., "My Awesome World"
    std::string directoryName;   // Filesystem-safe name, e.g., "my_awesome_world_1678886400"
    std::string generatorId;     // Identifier for the world generator used
    int64_t seed;                // Seed for procedural generation
    int64_t lastPlayedTimestamp; // Unix timestamp of the last time the world was played
    int64_t creationTimestamp;   // Unix timestamp of when the world was created
};

// This macro automatically generates 'to_json' and 'from_json' functions
// marked as inline, so they can live safely in this header.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorldMetadata, worldName, directoryName, generatorId, seed, lastPlayedTimestamp, creationTimestamp)

namespace WorldSave {

    /**
     * @brief Saves WorldMetadata to a JSON file at the specified path.
     */
    inline bool saveMetadata(const WorldMetadata& metadata, const std::string& path) {
        try {
            nlohmann::json j = metadata; // This automatically calls our to_json function
            std::ofstream file(path);
            if (!file.is_open()) {
                std::cerr << "Error: Could not open file for writing: " << path << std::endl;
                return false;
            }
            file << j.dump(4); // pretty-print with 4-space indent
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error saving metadata to " << path << ": " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * @brief Loads WorldMetadata from a JSON file at the specified path.
     */
    inline std::optional<WorldMetadata> loadMetadata(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return std::nullopt; // File not found is not an error, just means no metadata.
            }
            nlohmann::json j;
            file >> j;
            return j.get<WorldMetadata>(); // This automatically calls our from_json function
        } catch (const std::exception& e) {
            std::cerr << "Error loading metadata from " << path << ": " << e.what() << std::endl;
            return std::nullopt;
        }
    }
} // namespace WorldSave