#pragma once

#include <string>
#include <cstdint>

/**
 * @brief A lightweight structure to hold metadata about a saved world.
 * This data is loaded quickly to populate the world selection menu.
 */
struct WorldMetadata {
    std::string worldName;       // User-facing name, e.g., "My Awesome World"
    std::string directoryName;   // Filesystem-safe name, e.g., "my_awesome_world_1678886400"
    int64_t seed;                // Seed for procedural generation
    int64_t lastPlayedTimestamp; // Unix timestamp of the last time the world was played
    int64_t creationTimestamp;   // Unix timestamp of when the world was created
};