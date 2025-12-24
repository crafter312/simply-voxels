#pragma once

#include "ITerrainGenerator.hpp"
#include <wasm.h>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief A terrain generator that executes compiled WebAssembly code.
 */
class WasmTerrainGenerator : public ITerrainGenerator {
public:
    /**
     * @brief Loads and compiles the WASM module.
     * @throws std::runtime_error if loading, compiling, or linking fails.
     */
    WasmTerrainGenerator(wasm_engine_t* engine, const std::string& wasmFilePath);
    ~WasmTerrainGenerator() override;

    // Delete copy constructors to prevent double-freeing WASM resources
    WasmTerrainGenerator(const WasmTerrainGenerator&) = delete;
    WasmTerrainGenerator& operator=(const WasmTerrainGenerator&) = delete;

    void generateChunk(Chunk& chunk, int64_t seed) override;

private:
    struct WasmStoreDeleter { void operator()(wasm_store_t* p) const { wasm_store_delete(p); } };
    struct WasmModuleDeleter { void operator()(wasm_module_t* p) const { wasm_module_delete(p); } };
    struct WasmInstanceDeleter { void operator()(wasm_instance_t* p) const { wasm_instance_delete(p); } };

    std::unique_ptr<wasm_store_t, WasmStoreDeleter> m_store;
    std::unique_ptr<wasm_module_t, WasmModuleDeleter> m_module;
    std::unique_ptr<wasm_instance_t, WasmInstanceDeleter> m_instance;
    
    // Pointers to exports (owned by the instance/store)
    const wasm_func_t* m_generateFunc = nullptr;
    wasm_memory_t* m_memory = nullptr;
    uint32_t m_memoryOffset = 0;

    std::vector<uint8_t> loadWasmFile(const std::string& path);
};