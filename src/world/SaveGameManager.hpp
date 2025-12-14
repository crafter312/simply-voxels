#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "WorldMetadata.hpp"

/**
 * @brief Manages the discovery, creation, and deletion of world save files.
 *
 * This class acts as the "librarian" for all saved worlds. It scans a directory
 * for world folders, loads their metadata for display in the UI, and handles
 * the file system operations for creating and deleting worlds.
 */
class SaveGameManager {
public:
    // Constructor will eventually scan for worlds.
    explicit SaveGameManager(const std::string& savesPath);

    // Public interface to be implemented later.
    /**
     * @brief Gets a list of all discovered worlds, sorted by last played time.
     * @return A constant reference to the vector of world metadata.
     */
    const std::vector<WorldMetadata>& getAvailableWorlds() const { return m_worlds; }

    WorldMetadata createNewWorld(const std::string& worldName, std::optional<int64_t> seed);

    /**
     * @brief Deletes a world from the filesystem.
     * @param directoryName The unique directory name of the world to delete.
     */
    void deleteWorld(const std::string& directoryName);

    /**
     * @brief Updates the last played timestamp for a world.
     * This should be called when a world is loaded or saved.
     * @param directoryName The unique directory name of the world to update.
     */
    void updateLastPlayed(const std::string& directoryName);

private:
    std::string m_savesPath;
    std::vector<WorldMetadata> m_worlds;

    std::string sanitizeNameForDirectory(const std::string& name);
};
