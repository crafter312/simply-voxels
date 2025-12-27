#include "TerrainGeneratorManager.hpp"

#include <iostream>
#include <fstream>
#include <wasmtime.h>

#include "ITerrainGenerator.hpp"
#include "SimplePerlinGenerator.hpp"
#include "SimpleFlatGenerator.hpp"
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
        else if (entry.metadata.id == "simpleFlatGenerator") {
            return std::make_unique<SimpleFlatGenerator>();
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
    defaultEntry.metadata.author = "crafter312";
    defaultEntry.isBuiltIn = true;

    m_availableGenerators.push_back(defaultEntry);
    m_idToIndexMap[defaultEntry.metadata.id] = m_availableGenerators.size() - 1;

    TerrainGeneratorEntry simpleFlatGenerator;
    simpleFlatGenerator.metadata.id = "simpleFlatGenerator";
    simpleFlatGenerator.metadata.name = "Simple Flat (Built-in)";
    simpleFlatGenerator.metadata.description = "Basic flat world generator.";
    simpleFlatGenerator.metadata.version = "1.0";
    simpleFlatGenerator.metadata.author = "crafter312";
    simpleFlatGenerator.isBuiltIn = true;

    m_availableGenerators.push_back(simpleFlatGenerator);
    m_idToIndexMap[simpleFlatGenerator.metadata.id] = m_availableGenerators.size() - 1;
}

void TerrainGeneratorManager::scanForWasmGenerators() {
    const fs::path generatorsDir = "resources/terrain_generators";

    if (!fs::exists(generatorsDir) || !fs::is_directory(generatorsDir)) {
        std::cerr << "[TerrainGeneratorManager] Directory '" << generatorsDir << "' does not exist or is not a directory." << std::endl;
        return;
    }

    std::cout << "[TerrainGeneratorManager] Scanning for WASM terrain generators in '" << generatorsDir << "'..." << std::endl;
    for (const auto& entry : fs::directory_iterator(generatorsDir)) {
        std::cout << "[TerrainGeneratorManager] Checking entry: " << entry.path() << std::endl;
        if (!entry.is_directory()) {
            std::cerr << "[TerrainGeneratorManager] Skipping non-directory entry: " << entry.path() << std::endl;
            continue;
        }

        fs::path metadataPath = entry.path() / "metadata.json";
        fs::path wasmPath = entry.path() / "generator.wasm";

        if (!fs::exists(metadataPath) || !fs::exists(wasmPath)) {
            std::cerr << "[TerrainGeneratorManager] Skipping entry without required files: " << entry.path() << std::endl;
            continue;
        }

        auto metaOpt = GeneratorSave::loadMetadata(metadataPath.string());
        if (!metaOpt) {
            std::cerr << "[TerrainGeneratorManager] Failed to load metadata for generator at: " << entry.path() << std::endl;
            continue;
        }

        TerrainGeneratorEntry genEntry;
        genEntry.metadata = *metaOpt;
        genEntry.metadata.id = entry.path().filename().string();
        genEntry.isBuiltIn = false;
        genEntry.wasmPath = wasmPath;

        if (m_idToIndexMap.count(genEntry.metadata.id)) continue;

        m_availableGenerators.push_back(genEntry);
        m_idToIndexMap[genEntry.metadata.id] = m_availableGenerators.size() - 1;
        std::cout << "[TerrainGeneratorManager] Registered WASM generator: " << genEntry.metadata.name << " (ID: " << genEntry.metadata.id << ")" << std::endl;
    }
}