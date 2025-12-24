#include "TerrainGeneratorManager.hpp"

#include <iostream>
#include <fstream>
#include <wasmtime.h>

#include "ITerrainGenerator.hpp"
#include "SimplePerlinGenerator.hpp"
#include "WasmTerrainGenerator.hpp"

namespace fs = std::filesystem;

TerrainGeneratorManager::TerrainGeneratorManager() = default;

TerrainGeneratorManager::~TerrainGeneratorManager() {
    if (m_engine) {
        wasm_engine_delete(m_engine);
        m_engine = nullptr;
    }
}

void TerrainGeneratorManager::initialize() {
    // Initialize Wasmtime engine
    m_engine = wasm_engine_new();
    if (!m_engine) {
        std::cerr << "Failed to create Wasmtime engine." << std::endl;
    }

    m_availableGenerators.clear();
    m_idToIndexMap.clear();

    registerBuiltInGenerators();
    scanForWasmGenerators();
}

const std::vector<TerrainGeneratorEntry>& TerrainGeneratorManager::getAvailableGenerators() const {
    return m_availableGenerators;
}

bool TerrainGeneratorManager::hasGenerator(const std::string& id) const {
    return m_idToIndexMap.find(id) != m_idToIndexMap.end();
}

std::unique_ptr<ITerrainGenerator> TerrainGeneratorManager::createGenerator(const std::string& id) {
    auto it = m_idToIndexMap.find(id);
    if (it == m_idToIndexMap.end()) {
        std::cerr << "Generator ID '" << id << "' not found. Falling back to default." << std::endl;
        return std::make_unique<SimplePerlinGenerator>();
    }

    const auto& entry = m_availableGenerators[it->second];

    if (entry.isBuiltIn) {
        if (entry.metadata.id == "default") {
            return std::make_unique<SimplePerlinGenerator>();
        }
        // Future built-in generators can be added here
    } else {
        if (m_engine) {
            return std::make_unique<WasmTerrainGenerator>(m_engine, entry.wasmPath.string());
        } else {
            std::cerr << "Cannot create WASM generator: Engine not initialized." << std::endl;
        }
    }

    return std::make_unique<SimplePerlinGenerator>();
}

void TerrainGeneratorManager::registerBuiltInGenerators() {
    TerrainGeneratorEntry defaultEntry;
    defaultEntry.metadata.id = "default";
    defaultEntry.metadata.name = "Simple Perlin (Built-in)";
    defaultEntry.metadata.description = "Standard CPU-based Perlin noise generator.";
    defaultEntry.metadata.version = "1.0";
    defaultEntry.metadata.author = "Simply Voxels";
    defaultEntry.isBuiltIn = true;

    m_availableGenerators.push_back(defaultEntry);
    m_idToIndexMap[defaultEntry.metadata.id] = m_availableGenerators.size() - 1;
}

void TerrainGeneratorManager::scanForWasmGenerators() {
    const fs::path generatorsDir = "resources/terrain_generators";

    if (!fs::exists(generatorsDir) || !fs::is_directory(generatorsDir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(generatorsDir)) {
        if (!entry.is_directory()) continue;

        fs::path metadataPath = entry.path() / "metadata.json";
        fs::path wasmPath = entry.path() / "generator.wasm";

        if (!fs::exists(metadataPath) || !fs::exists(wasmPath)) continue;

        auto metaOpt = GeneratorSave::loadMetadata(metadataPath.string());
        if (!metaOpt) continue;

        TerrainGeneratorEntry genEntry;
        genEntry.metadata = *metaOpt;
        genEntry.metadata.id = entry.path().filename().string();
        genEntry.isBuiltIn = false;
        genEntry.wasmPath = wasmPath;

        if (m_idToIndexMap.count(genEntry.metadata.id)) continue;

        m_availableGenerators.push_back(genEntry);
        m_idToIndexMap[genEntry.metadata.id] = m_availableGenerators.size() - 1;
    }
}