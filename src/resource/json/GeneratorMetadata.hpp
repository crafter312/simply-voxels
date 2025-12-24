#pragma once

#include <string>
#include <optional>
#include <fstream>
#include <iostream>
#include "json.hpp"

/**
 * @brief Metadata describing a terrain generator resource.
 */
struct GeneratorMetadata {
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    
    // ID is typically derived from the directory name, not the JSON itself,
    // so it is not included in the macro below.
    std::string id; 
};

// Only map the fields present in the JSON file
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GeneratorMetadata, name, author, version, description)

namespace GeneratorSave {

    /**
     * @brief Saves GeneratorMetadata to a JSON file at the specified path.
     */
    inline bool saveMetadata(const GeneratorMetadata& metadata, const std::string& path) {
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
     * @brief Loads GeneratorMetadata from a JSON file at the specified path.
     */
    inline std::optional<GeneratorMetadata> loadMetadata(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return std::nullopt; // File not found is not an error, just means no metadata.
            }
            nlohmann::json j;
            file >> j;
            return j.get<GeneratorMetadata>(); // This automatically calls our from_json function
        } catch (const std::exception& e) {
            std::cerr << "Error loading metadata from " << path << ": " << e.what() << std::endl;
            return std::nullopt;
        }
    }
} // namespace GeneratorSave