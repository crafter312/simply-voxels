#include "WasmTerrainGenerator.hpp"
#include "../Chunk.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring> // For std::memcpy
#include <random>

// Global pointer for callback to access memory (set before _initialize is called)
// This is set by WasmTerrainGenerator before calling _initialize
static wasm_memory_t* g_wasmMemoryForCallback = nullptr;

// WASI stub callbacks - these satisfy wasi-sdk's minimal runtime requirements
// without actually implementing full WASI functionality.

// random_get(buf: i32, buf_len: i32) -> i32 (errno)
// Fills buffer with random bytes. Used by __wasilibc_init_ssp for stack protection.
static wasm_trap_t* wasi_random_get_callback(
    void* env, const wasm_val_vec_t* args, wasm_val_vec_t* results
) {
    std::cout << "[WASM CALLBACK] random_get called" << std::endl;
    std::cout.flush();

    (void)env; // Unused - we use global g_wasmMemoryForCallback instead

    int32_t buf_ptr = args->data[0].of.i32;
    int32_t buf_len = args->data[1].of.i32;

    std::cout << "[WASM CALLBACK] random_get: buf_ptr=" << buf_ptr << ", buf_len=" << buf_len << std::endl;
    std::cout.flush();

    wasm_memory_t* memory = g_wasmMemoryForCallback;

    std::cout << "[WASM CALLBACK] random_get: g_wasmMemoryForCallback=" << (void*)memory << std::endl;
    std::cout.flush();

    if (!memory) {
        std::cout << "[WASM CALLBACK] random_get: ERROR - memory is null!" << std::endl;
        std::cout.flush();
        // Memory not available yet, return error
        results->data[0].kind = WASM_I32;
        results->data[0].of.i32 = 28; // EINVAL
        return nullptr;
    }

    uint8_t* mem_data = reinterpret_cast<uint8_t*>(wasm_memory_data(memory));
    size_t mem_size = wasm_memory_data_size(memory);

    std::cout << "[WASM CALLBACK] random_get: mem_data=" << (void*)mem_data << ", mem_size=" << mem_size << std::endl;
    std::cout.flush();

    // Bounds check
    if (buf_ptr < 0 || buf_len < 0 || static_cast<size_t>(buf_ptr + buf_len) > mem_size) {
        std::cout << "[WASM CALLBACK] random_get: ERROR - bounds check failed!" << std::endl;
        std::cout.flush();
        results->data[0].kind = WASM_I32;
        results->data[0].of.i32 = 28; // EINVAL
        return nullptr;
    }

    // Fill with random bytes using C++ random
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 255);

    for (int32_t i = 0; i < buf_len; ++i) {
        mem_data[buf_ptr + i] = static_cast<uint8_t>(dist(gen));
    }

    std::cout << "[WASM CALLBACK] random_get: SUCCESS - filled " << buf_len << " bytes" << std::endl;
    std::cout.flush();

    results->data[0].kind = WASM_I32;
    results->data[0].of.i32 = 0; // Success
    return nullptr;
}

// proc_exit(code: i32) -> noreturn
// Called on fatal errors. We trap instead of actually exiting.
static wasm_trap_t* wasi_proc_exit_callback(
    void* env, const wasm_val_vec_t* args, wasm_val_vec_t* results
) {
    std::cout << "[WASM CALLBACK] proc_exit called" << std::endl;
    std::cout.flush();

    (void)env;
    (void)results;
    int32_t exit_code = args->data[0].of.i32;

    std::cout << "[WASM CALLBACK] proc_exit: exit_code=" << exit_code << std::endl;
    std::cout.flush();

    // Create a trap to abort execution
    wasm_message_t message;
    std::string msg = "WASM called proc_exit with code " + std::to_string(exit_code);
    wasm_byte_vec_new(&message, msg.size() + 1, msg.c_str());
    wasm_trap_t* trap = wasm_trap_new(nullptr, &message);
    wasm_byte_vec_delete(&message);
    return trap;
}

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
    std::cout << "[WASM] === Starting WasmTerrainGenerator constructor ===" << std::endl;
    std::cout << "[WASM] File: " << wasmFilePath << std::endl;
    std::cout.flush();

    // 1. Initialize Store
    std::cout << "[WASM] Step 1: Creating store..." << std::endl;
    std::cout.flush();
    m_store.reset(wasm_store_new(engine));
    if (!m_store) throw std::runtime_error("Failed to create WASM store");
    std::cout << "[WASM] Step 1: Store created OK" << std::endl;
    std::cout.flush();

    // 2. Load and Compile Module
    std::cout << "[WASM] Step 2: Loading WASM file..." << std::endl;
    std::cout.flush();
    auto wasmBytes = loadWasmFile(wasmFilePath);
    std::cout << "[WASM] Step 2: Loaded " << wasmBytes.size() << " bytes" << std::endl;
    std::cout.flush();

    wasm_byte_vec_t binary;
    wasm_byte_vec_new(&binary, wasmBytes.size(), reinterpret_cast<const char*>(wasmBytes.data()));

    std::cout << "[WASM] Step 2: Compiling module..." << std::endl;
    std::cout.flush();
    m_module.reset(wasm_module_new(m_store.get(), &binary));
    wasm_byte_vec_delete(&binary); // Clean up the vector wrapper

    if (!m_module) throw std::runtime_error("Failed to compile WASM module: " + wasmFilePath);
    std::cout << "[WASM] Step 2: Module compiled OK" << std::endl;
    std::cout.flush();

    // 3. Query what imports the module needs
    std::cout << "[WASM] Step 3: Querying module imports..." << std::endl;
    std::cout.flush();
    wasm_importtype_vec_t import_types;
    wasm_module_imports(m_module.get(), &import_types);
    std::cout << "[WASM] Step 3: Module requires " << import_types.size << " imports:" << std::endl;
    std::cout.flush();

    // List all imports for debugging
    for (size_t i = 0; i < import_types.size; ++i) {
        const wasm_name_t* module_name = wasm_importtype_module(import_types.data[i]);
        const wasm_name_t* name = wasm_importtype_name(import_types.data[i]);
        std::string modStr(module_name->data, module_name->size);
        std::string nameStr(name->data, name->size);
        std::cout << "[WASM]   [" << i << "] " << modStr << "::" << nameStr << std::endl;
    }
    std::cout.flush();

    // 4. Create WASI stub functions to satisfy imports
    std::cout << "[WASM] Step 4: Creating WASI stubs..." << std::endl;
    std::cout.flush();
    createWasiStubs();
    std::cout << "[WASM] Step 4: WASI stubs created OK" << std::endl;
    std::cout.flush();

    // 5. Build imports array matching what the module expects
    std::cout << "[WASM] Step 5: Building imports array..." << std::endl;
    std::cout.flush();
    std::vector<wasm_extern_t*> import_externs;
    import_externs.reserve(import_types.size);

    for (size_t i = 0; i < import_types.size; ++i) {
        const wasm_name_t* module_name = wasm_importtype_module(import_types.data[i]);
        const wasm_name_t* name = wasm_importtype_name(import_types.data[i]);

        std::string modStr(module_name->data, module_name->size);
        std::string nameStr(name->data, name->size);

        std::cout << "[WASM]   Matching [" << i << "] " << modStr << "::" << nameStr << "..." << std::endl;
        std::cout.flush();

        if (modStr == "wasi_snapshot_preview1") {
            if (nameStr == "random_get") {
                std::cout << "[WASM]     -> random_get stub" << std::endl;
                import_externs.push_back(wasm_func_as_extern(m_wasiRandomGet.get()));
            } else if (nameStr == "proc_exit") {
                std::cout << "[WASM]     -> proc_exit stub" << std::endl;
                import_externs.push_back(wasm_func_as_extern(m_wasiProcExit.get()));
            } else {
                std::cout << "[WASM]     -> UNKNOWN! Throwing..." << std::endl;
                std::cout.flush();
                wasm_importtype_vec_delete(&import_types);
                throw std::runtime_error("Unknown WASI import required: " + nameStr);
            }
        } else {
            std::cout << "[WASM]     -> UNKNOWN MODULE! Throwing..." << std::endl;
            std::cout.flush();
            wasm_importtype_vec_delete(&import_types);
            throw std::runtime_error("Unknown import module: " + modStr + "::" + nameStr);
        }
        std::cout.flush();
    }
    wasm_importtype_vec_delete(&import_types);
    std::cout << "[WASM] Step 5: Built " << import_externs.size() << " imports OK" << std::endl;
    std::cout.flush();

    // 6. Instantiate Module with imports
    std::cout << "[WASM] Step 6: Instantiating module..." << std::endl;
    std::cout.flush();
    wasm_extern_vec_t imports = { import_externs.size(), import_externs.data() };

    wasm_trap_t* trap = nullptr;
    std::cout << "[WASM] Step 6: Calling wasm_instance_new()..." << std::endl;
    std::cout.flush();
    m_instance.reset(wasm_instance_new(m_store.get(), m_module.get(), &imports, &trap));
    std::cout << "[WASM] Step 6: wasm_instance_new() returned" << std::endl;
    std::cout.flush();

    if (trap) {
        std::cout << "[WASM] Step 6: TRAP during instantiation!" << std::endl;
        std::cout.flush();
        wasm_message_t msg;
        wasm_trap_message(trap, &msg);
        std::string errorMsg(msg.data, msg.size - 1); // Remove null terminator
        wasm_byte_vec_delete(&msg);
        wasm_trap_delete(trap);
        throw std::runtime_error("WASM Instantiation Trap: " + errorMsg);
    }
    if (!m_instance) throw std::runtime_error("Failed to instantiate WASM module");
    std::cout << "[WASM] Step 6: Module instantiated OK" << std::endl;
    std::cout.flush();

    // 7. Inspect Exports to find 'generate', '_initialize', and 'memory'
    std::cout << "[WASM] Step 7: Inspecting exports..." << std::endl;
    std::cout.flush();
    wasm_extern_vec_t exports;
    wasm_instance_exports(m_instance.get(), &exports);

    wasm_exporttype_vec_t export_types;
    wasm_module_exports(m_module.get(), &export_types);

    std::cout << "[WASM] Step 7: Module has " << exports.size << " exports:" << std::endl;
    std::cout.flush();

    for (size_t i = 0; i < exports.size; ++i) {
        const wasm_name_t* name = wasm_exporttype_name(export_types.data[i]);
        wasm_extern_t* item = exports.data[i];
        wasm_externkind_t kind = wasm_extern_kind(item);

        std::string nameStr(name->data, name->size);
        std::cout << "[WASM]   [" << i << "] '" << nameStr << "' kind=" << kind << std::endl;

        bool kept = false;

        if (kind == WASM_EXTERN_FUNC && nameStr == "generate") {
            m_generateFunc = wasm_extern_as_func(item);
            kept = true;
            std::cout << "[WASM]       -> Kept as generate" << std::endl;
        } else if (kind == WASM_EXTERN_FUNC && nameStr == "_initialize") {
            m_initializeFunc = wasm_extern_as_func(item);
            kept = true;
            std::cout << "[WASM]       -> Kept as _initialize" << std::endl;
        } else if (kind == WASM_EXTERN_MEMORY && nameStr == "memory") {
            m_memory = wasm_extern_as_memory(item);
            kept = true;
            std::cout << "[WASM]       -> Kept as memory" << std::endl;
        }

        // Note: We do NOT delete individual exports here. The individual export
        // objects (wasm_extern_t*) are owned by the instance, not by us.
        // We just keep pointers to the ones we need.
        std::cout.flush();
    }
    wasm_exporttype_vec_delete(&export_types);
    // NOTE: We intentionally do NOT call wasm_extern_vec_delete(&exports) here.
    // Although the documentation says we "own" the vector, calling
    // wasm_extern_vec_delete would delete the wasm_extern_t* objects inside,
    // which would invalidate our pointers (m_generateFunc, m_initializeFunc,
    // m_memory). The exports are managed by the instance and will be cleaned
    // up when the instance is destroyed.

    std::cout << "[WASM] Step 7: generate=" << (m_generateFunc ? "YES" : "NO")
              << " _initialize=" << (m_initializeFunc ? "YES" : "NO")
              << " memory=" << (m_memory ? "YES" : "NO") << std::endl;
    std::cout.flush();

    if (!m_generateFunc) throw std::runtime_error("WASM module does not export 'generate' function");
    if (!m_memory) throw std::runtime_error("WASM module does not export 'memory'");

    // 8. Call _initialize if exported (required for reactor modules)
    // This initializes the C++ runtime (static constructors, etc.)
    // Set global memory pointer so random_get callback can access it
    std::cout << "[WASM] Step 8: Setting g_wasmMemoryForCallback..." << std::endl;
    std::cout.flush();
    g_wasmMemoryForCallback = m_memory;
    std::cout << "[WASM] Step 8: g_wasmMemoryForCallback = " << (void*)m_memory << std::endl;
    std::cout.flush();

    if (m_initializeFunc) {
        std::cout << "[WASM] Step 8: Calling _initialize()..." << std::endl;
        std::cout.flush();
        wasm_val_vec_t empty_args = WASM_EMPTY_VEC;
        wasm_val_vec_t empty_results = WASM_EMPTY_VEC;
        trap = wasm_func_call(m_initializeFunc, &empty_args, &empty_results);
        std::cout << "[WASM] Step 8: _initialize() returned" << std::endl;
        std::cout.flush();
        if (trap) {
            std::cout << "[WASM] Step 8: TRAP in _initialize!" << std::endl;
            std::cout.flush();
            g_wasmMemoryForCallback = nullptr;
            wasm_message_t msg;
            wasm_trap_message(trap, &msg);
            std::string errorMsg(msg.data, msg.size - 1);
            wasm_byte_vec_delete(&msg);
            wasm_trap_delete(trap);
            throw std::runtime_error("WASM _initialize trap: " + errorMsg);
        }
        std::cout << "[WASM] Step 8: _initialize() completed OK" << std::endl;
        std::cout.flush();
    } else {
        std::cout << "[WASM] Step 8: No _initialize to call" << std::endl;
        std::cout.flush();
    }

    // Keep memory pointer set for any future WASI calls during generate()
    // (though generate() shouldn't need random_get)

    // 9. Validate Signature: generate(i32, i32, i32, i64, i32) -> void
    std::cout << "[WASM] Step 9: Validating generate() signature..." << std::endl;
    std::cout.flush();
    wasm_functype_t* funcType = wasm_func_type(m_generateFunc);
    const wasm_valtype_vec_t* params = wasm_functype_params(funcType);
    const wasm_valtype_vec_t* results = wasm_functype_results(funcType);

    std::cout << "[WASM] Step 9: params=" << params->size << " results=" << results->size << std::endl;
    std::cout.flush();

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
    std::cout << "[WASM] Step 9: Signature valid OK" << std::endl;
    std::cout.flush();

    // 10. Allocate Buffer in WASM Memory
    std::cout << "[WASM] Step 10: Allocating buffer in WASM memory..." << std::endl;
    std::cout.flush();
    size_t currentPages = wasm_memory_size(m_memory);
    std::cout << "[WASM] Step 10: Current pages = " << currentPages << std::endl;
    std::cout.flush();
    if (!wasm_memory_grow(m_memory, 1)) {
        throw std::runtime_error("Failed to grow WASM memory for chunk buffer");
    }
    // The offset is the start of the new page (previous size * page size)
    m_memoryOffset = static_cast<uint32_t>(currentPages * 65536);
    std::cout << "[WASM] Step 10: Memory grown, offset = " << m_memoryOffset << std::endl;
    std::cout.flush();

    std::cout << "[WASM] === Constructor completed successfully! ===" << std::endl;
    std::cout.flush();
}

WasmTerrainGenerator::~WasmTerrainGenerator() {
    // Clear the global memory pointer if it points to our memory
    if (g_wasmMemoryForCallback == m_memory) {
        g_wasmMemoryForCallback = nullptr;
    }
    // Note: m_generateFunc, m_initializeFunc, and m_memory are NOT owned by us.
    // They are owned by the wasm_instance_t (m_instance) and will be cleaned up
    // when m_instance is destroyed by its unique_ptr deleter.
    // We must NOT call wasm_func_delete() or wasm_memory_delete() on them.

    // m_wasiRandomGet and m_wasiProcExit ARE owned by us (we created them with
    // wasm_func_new_with_env) and are cleaned up by their unique_ptr deleters.
}

void WasmTerrainGenerator::createWasiStubs() {
    std::cout << "[WASM] createWasiStubs: Creating random_get type..." << std::endl;
    std::cout.flush();

    // Create function type for random_get: (i32, i32) -> i32
    // Must use wasm_valtype_vec_new_uninitialized and fill in the data
    wasm_valtype_vec_t random_get_params_vec;
    wasm_valtype_vec_new_uninitialized(&random_get_params_vec, 2);
    random_get_params_vec.data[0] = wasm_valtype_new(WASM_I32);
    random_get_params_vec.data[1] = wasm_valtype_new(WASM_I32);

    wasm_valtype_vec_t random_get_results_vec;
    wasm_valtype_vec_new_uninitialized(&random_get_results_vec, 1);
    random_get_results_vec.data[0] = wasm_valtype_new(WASM_I32);

    std::cout << "[WASM] createWasiStubs: Creating random_get functype..." << std::endl;
    std::cout.flush();

    // Note: wasm_functype_new takes ownership of the vectors' contents
    wasm_functype_t* random_get_type = wasm_functype_new(&random_get_params_vec, &random_get_results_vec);
    std::cout << "[WASM] createWasiStubs: random_get_type = " << (void*)random_get_type << std::endl;
    std::cout.flush();

    std::cout << "[WASM] createWasiStubs: Creating proc_exit type..." << std::endl;
    std::cout.flush();

    // Create function type for proc_exit: (i32) -> ()
    wasm_valtype_vec_t proc_exit_params_vec;
    wasm_valtype_vec_new_uninitialized(&proc_exit_params_vec, 1);
    proc_exit_params_vec.data[0] = wasm_valtype_new(WASM_I32);

    wasm_valtype_vec_t proc_exit_results_vec;
    wasm_valtype_vec_new_empty(&proc_exit_results_vec);

    std::cout << "[WASM] createWasiStubs: Creating proc_exit functype..." << std::endl;
    std::cout.flush();

    wasm_functype_t* proc_exit_type = wasm_functype_new(&proc_exit_params_vec, &proc_exit_results_vec);
    std::cout << "[WASM] createWasiStubs: proc_exit_type = " << (void*)proc_exit_type << std::endl;
    std::cout.flush();

    // Create the stub functions
    std::cout << "[WASM] createWasiStubs: Creating random_get function..." << std::endl;
    std::cout.flush();
    m_wasiRandomGet.reset(wasm_func_new_with_env(
        m_store.get(),
        random_get_type,
        wasi_random_get_callback,
        this,  // Pass 'this' so callback can access m_memory after it's set
        nullptr
    ));
    std::cout << "[WASM] createWasiStubs: m_wasiRandomGet = " << (void*)m_wasiRandomGet.get() << std::endl;
    std::cout.flush();

    std::cout << "[WASM] createWasiStubs: Creating proc_exit function..." << std::endl;
    std::cout.flush();
    m_wasiProcExit.reset(wasm_func_new_with_env(
        m_store.get(),
        proc_exit_type,
        wasi_proc_exit_callback,
        nullptr,
        nullptr
    ));
    std::cout << "[WASM] createWasiStubs: m_wasiProcExit = " << (void*)m_wasiProcExit.get() << std::endl;
    std::cout.flush();

    wasm_functype_delete(random_get_type);
    wasm_functype_delete(proc_exit_type);

    std::cout << "[WASM] createWasiStubs: Done" << std::endl;
    std::cout.flush();
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