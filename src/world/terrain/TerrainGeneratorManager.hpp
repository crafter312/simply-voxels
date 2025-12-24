#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>

#include "../../resource/json/GeneratorMetadata.hpp"

// Forward declarations
struct wasm_engine_t;
class ITerrainGenerator;

struct TerrainGeneratorEntry {
    GeneratorMetadata metadata;
    bool isBuiltIn = false;
    std::filesystem::path wasmPath;
};

class TerrainGeneratorManager {
public:
    TerrainGeneratorManager();
    ~TerrainGeneratorManager();

    // Initialize the manager: setup Wasmtime engine and scan for generators
    void initialize();

    // Get list of all registered generators
    const std::vector<TerrainGeneratorEntry>& getAvailableGenerators() const;

    // Factory method to create a generator instance
    std::unique_ptr<ITerrainGenerator> createGenerator(const std::string& id);

private:
    void registerBuiltInGenerators();
    void scanForWasmGenerators();

    wasm_engine_t* m_engine = nullptr;
    std::vector<TerrainGeneratorEntry> m_availableGenerators;
    std::unordered_map<std::string, size_t> m_idToIndexMap;
};