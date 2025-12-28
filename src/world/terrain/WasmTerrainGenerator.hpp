#pragma once

#include "ITerrainGenerator.hpp"
#include <wasm.h>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief A terrain generator that executes compiled WebAssembly code.
 *
 * Uses wasi-sdk compiled WASM modules in reactor mode. Provides minimal
 * WASI stubs (random_get, proc_exit) required by the wasi-libc runtime.
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
    struct WasmFuncDeleter { void operator()(wasm_func_t* p) const { wasm_func_delete(p); } };

    // IMPORTANT: Declaration order matters! C++ destroys members in reverse order.
    // The instance holds references to the WASI stubs (as imports), so the instance
    // must be destroyed BEFORE the WASI stubs. The module and store must be destroyed
    // after everything that depends on them.
    //
    // Correct destruction order: instance -> wasiProcExit -> wasiRandomGet -> module -> store
    // Therefore declaration order must be: store -> module -> wasiRandomGet -> wasiProcExit -> instance

    std::unique_ptr<wasm_store_t, WasmStoreDeleter> m_store;
    std::unique_ptr<wasm_module_t, WasmModuleDeleter> m_module;

    // WASI stub functions (we own these) - must be declared BEFORE instance
    // because the instance imports them and holds references to them
    std::unique_ptr<wasm_func_t, WasmFuncDeleter> m_wasiRandomGet;
    std::unique_ptr<wasm_func_t, WasmFuncDeleter> m_wasiProcExit;

    // Instance must be declared LAST among WASM objects so it's destroyed FIRST
    std::unique_ptr<wasm_instance_t, WasmInstanceDeleter> m_instance;

    // Pointers to exports (owned by the instance/store)
    const wasm_func_t* m_generateFunc = nullptr;
    const wasm_func_t* m_initializeFunc = nullptr;
    wasm_memory_t* m_memory = nullptr;
    uint32_t m_memoryOffset = 0;

    std::vector<uint8_t> loadWasmFile(const std::string& path);

    // Creates WASI stub functions needed by wasi-sdk compiled modules
    void createWasiStubs();
};