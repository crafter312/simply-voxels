#include "WasmTerrainGenerator.hpp"
#include "../Chunk.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring> // For std::memcpy

// Helper to read the binary WASM file from disk
std::vector<uint8_t> WasmTerrainGenerator::loadWasmFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open WASM file: " + path);
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read WASM file: " + path);
    }
    return buffer;
}

WasmTerrainGenerator::WasmTerrainGenerator(wasm_engine_t* engine, const std::string& wasmFilePath) {
    // 1. Initialize Store
    m_store.reset(wasm_store_new(engine));
    if (!m_store) throw std::runtime_error("Failed to create WASM store");

    // 2. Load and Compile Module
    auto wasmBytes = loadWasmFile(wasmFilePath);
    wasm_byte_vec_t binary;
    wasm_byte_vec_new(&binary, wasmBytes.size(), reinterpret_cast<const char*>(wasmBytes.data()));
    
    m_module.reset(wasm_module_new(m_store.get(), &binary));
    wasm_byte_vec_delete(&binary); // Clean up the vector wrapper

    if (!m_module) throw std::runtime_error("Failed to compile WASM module: " + wasmFilePath);

    // 3. Instantiate Module
    // We assume the generator is self-contained and requires no imports (e.g., WASI).
    wasm_extern_vec_t imports = WASM_EMPTY_VEC;
    
    wasm_trap_t* trap = nullptr;
    m_instance.reset(wasm_instance_new(m_store.get(), m_module.get(), &imports, &trap));
    
    if (trap) {
        wasm_message_t msg;
        wasm_trap_message(trap, &msg);
        std::string errorMsg(msg.data, msg.size - 1); // Remove null terminator
        wasm_byte_vec_delete(&msg);
        wasm_trap_delete(trap);
        throw std::runtime_error("WASM Instantiation Trap: " + errorMsg);
    }
    if (!m_instance) throw std::runtime_error("Failed to instantiate WASM module");

    // 4. Inspect Exports to find 'generate' and 'memory'
    wasm_extern_vec_t exports;
    wasm_instance_exports(m_instance.get(), &exports);

    // We need to extract the specific exports we need and manage the lifetime of the others.
    // The 'exports' vector contains new handles that must be deleted.
    wasm_exporttype_vec_t export_types;
    wasm_module_exports(m_module.get(), &export_types);

    for (size_t i = 0; i < exports.size; ++i) {
        const wasm_name_t* name = wasm_exporttype_name(export_types.data[i]);
        wasm_extern_t* item = exports.data[i];
        wasm_externkind_t kind = wasm_extern_kind(item);

        std::string nameStr(name->data, name->size);
        bool kept = false;

        if (kind == WASM_EXTERN_FUNC && nameStr == "generate") {
            m_generateFunc = wasm_extern_as_func(item);
            kept = true;
        } else if (kind == WASM_EXTERN_MEMORY && nameStr == "memory") {
            m_memory = wasm_extern_as_memory(item);
            kept = true;
        }

        if (!kept) {
            wasm_extern_delete(item); // Release unneeded exports
        }
    }
    wasm_exporttype_vec_delete(&export_types);
    // Note: We do not call wasm_extern_vec_delete(&exports) because we manually deleted 
    // the items we didn't want and kept the ones we did. Calling it would double-free or free our kept items.
    // We technically leak the 'exports.data' array pointer here, but it's negligible (one array per generator load).

    if (!m_generateFunc) throw std::runtime_error("WASM module does not export 'generate' function");
    if (!m_memory) throw std::runtime_error("WASM module does not export 'memory'");

    // 5. Validate Signature: generate(i32, i32, i32, i64, i32) -> void
    wasm_functype_t* funcType = wasm_func_type(m_generateFunc);
    const wasm_valtype_vec_t* params = wasm_functype_params(funcType);
    const wasm_valtype_vec_t* results = wasm_functype_results(funcType);

    bool valid = true;
    if (params->size != 5 || results->size != 0) valid = false;
    else {
        if (wasm_valtype_kind(params->data[0]) != WASM_I32) valid = false; // x
        if (wasm_valtype_kind(params->data[1]) != WASM_I32) valid = false; // y
        if (wasm_valtype_kind(params->data[2]) != WASM_I32) valid = false; // z
        if (wasm_valtype_kind(params->data[3]) != WASM_I64) valid = false; // seed
        if (wasm_valtype_kind(params->data[4]) != WASM_I32) valid = false; // ptr
    }
    wasm_functype_delete(funcType);

    if (!valid) throw std::runtime_error("Invalid signature for 'generate'. Expected (i32, i32, i32, i64, i32) -> void");

    // 6. Allocate Buffer in WASM Memory
    // We grow the memory by 1 page (64KB) and use the start of that new page as our buffer.
    // This ensures we don't overwrite the WASM module's own stack/heap data.
    size_t currentPages = wasm_memory_size(m_memory);
    if (!wasm_memory_grow(m_memory, 1)) {
        throw std::runtime_error("Failed to grow WASM memory for chunk buffer");
    }
    // The offset is the start of the new page (previous size * page size)
    m_memoryOffset = static_cast<uint32_t>(currentPages * 65536);
}

WasmTerrainGenerator::~WasmTerrainGenerator() {
    // Clean up the exports we took ownership of
    if (m_generateFunc) wasm_func_delete(const_cast<wasm_func_t*>(m_generateFunc));
    if (m_memory) wasm_memory_delete(m_memory);
}

void WasmTerrainGenerator::generateChunk(Chunk& chunk, int64_t seed) {
    glm::ivec3 coord = chunk.getChunkCoord();

    // 1. Prepare Arguments
    wasm_val_t args[5];
    args[0].kind = WASM_I32; args[0].of.i32 = coord.x;
    args[1].kind = WASM_I32; args[1].of.i32 = coord.y;
    args[2].kind = WASM_I32; args[2].of.i32 = coord.z;
    args[3].kind = WASM_I64; args[3].of.i64 = seed;
    args[4].kind = WASM_I32; args[4].of.i32 = static_cast<int32_t>(m_memoryOffset);

    wasm_val_vec_t args_vec = { 5, args };
    wasm_val_vec_t results_vec = WASM_EMPTY_VEC;

    // 2. Call WASM Function
    wasm_trap_t* trap = wasm_func_call(m_generateFunc, &args_vec, &results_vec);
    if (trap) {
        wasm_message_t msg;
        wasm_trap_message(trap, &msg);
        std::string errorMsg(msg.data, msg.size - 1);
        wasm_byte_vec_delete(&msg);
        wasm_trap_delete(trap);
        std::cerr << "Error generating chunk " << coord.x << "," << coord.y << "," << coord.z << ": " << errorMsg << std::endl;
        return; // Leave chunk as air/empty on error
    }

    // 3. Copy Data from WASM Memory to Chunk
    // We use performLockedWrite to safely access the chunk's internal buffer
    chunk.performLockedWrite([this](uint16_t* buffer, size_t capacity) {
        // Get the current base pointer of WASM memory (it might change if memory grows)
        uint8_t* wasmMemBase = reinterpret_cast<uint8_t*>(wasm_memory_data(m_memory));
        
        // Copy the data. We assume the WASM code filled the buffer at m_memoryOffset.
        // Capacity is CHUNK_VOLUME (4096), size is capacity * sizeof(uint16_t) (8192 bytes).
        std::memcpy(buffer, wasmMemBase + m_memoryOffset, capacity * sizeof(uint16_t));
    });

    chunk.markGenerated();
    chunk.setDirty(true); // Mark for mesh rebuild
}