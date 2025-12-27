# Configuration - Adjust these paths to match your setup
# Use LLVM (installed at C:\Program Files\LLVM) and libc++ from an emscripten install
$EMSCRIPTEN_ROOT = "C:/emscripten"
$GLM_PATH = "./build/_deps/glm-src" # Path to where your GLM headers are located
$SRC_DIR = "./src/wasm"
$OUT_DIR = "./resources/terrain_generators"
$INCLUDE_STUBS = $SRC_DIR + "/include_stubs" # Hacky empty include(s) to satisfy missing headers
$WASM_RUNTIME_DIR = $SRC_DIR + "/runtime"

# Compiler Setup - prefer system LLVM installation
$LLVM_ROOT = "C:/Program Files/LLVM"
$CLANG = Join-Path $LLVM_ROOT "bin/clang++.exe"

if (!(Test-Path $CLANG)) {
    Write-Host "Error: clang++ not found at $CLANG. Please install LLVM 21.1.0 or update the script." 
    exit 1
}

# Locate libc++ headers inside the provided emscripten install (user requested)
$libcxxInclude = $null
$envLibcxx = $env:LIBCXX_PATH
if ($envLibcxx) {
    $libcxxInclude = $envLibcxx
} elseif (Test-Path $EMSCRIPTEN_ROOT) {
    # common emscripten layout: C:\emscripten\system\lib\libcxx\include\c++\v1
    $try1 = Join-Path $EMSCRIPTEN_ROOT "system\lib\libcxx\include\c++\v1"
    $try2 = Join-Path $EMSCRIPTEN_ROOT "system\lib\libcxx\include"
    if (Test-Path $try1) { $libcxxInclude = $try1 }
    elseif (Test-Path $try2) { $libcxxInclude = $try2 }
}

# Clang builtin includes (optional)
$clangBuiltinInclude = Join-Path $LLVM_ROOT "lib/clang/21.1.0/include"
if (!(Test-Path $clangBuiltinInclude)) { $clangBuiltinInclude = $null }

# If libc++ headers not found, fail fast with guidance
if (-not $libcxxInclude) {
    Write-Host "Error: libc++ headers not found in EMSCRIPTEN_ROOT or LIBCXX_PATH."
    Write-Host "Please ensure libc++ headers exist under C:\\emscripten\\system\\lib\\libcxx\\include\\c++\\v1 or set LIBCXX_PATH." 
    exit 1
}

# Ensure output directory exists
if (!(Test-Path $OUT_DIR)) { New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null }

# Compile each .cpp file in the source directory
Get-ChildItem -Path $SRC_DIR -Filter *.cpp | ForEach-Object {
    $inputFile = $_.FullName
    $outputFile = Join-Path $OUT_DIR ($_.BaseName + ".wasm")
    
    Write-Host "Compiling $($_.Name) to WASM..."
    
    # Compile Command
    # --target=wasm32-wasi: Target WebAssembly with WASI support
    # -nostartfiles -Wl,--no-entry: Create a library, not an executable (no main())
    # -Wl,--export=generate: Export your specific function
    # -Wl,--export=memory: Export the memory so the host can write to it
    # -DNDEBUG: Disable assertions (prevents imports for abort/stderr)
    # -fno-math-errno: Math functions won't set errno (prevents imports for errno)
    $clangArgs = @(
        "--target=wasm32-unknown-unknown",
        "-stdlib=libc++",
        "-D_LIBCPP_HAS_NO_THREADS",
        "-D__EMSCRIPTEN__",
        "-D__EMSCRIPTEN_explicit_offline_build__",
        "-DNO_MALLINFO",
        "-D_WASI_EMULATED_SIGNAL",
        # Tell dlmalloc that we are providing the alignment
        "-D__DEFINED_max_align_t",
        "-O3",
        "-ffreestanding",
        "-nostdlib",
        "-fno-exceptions",
        "-flto",
        "-nostartfiles",
        "-Wl,--no-entry",
        # Export runtime items so the module is freestanding (no imports)
        "-Wl,--export=generate",
        "-Wl,--export-memory",
        "-Wl,--export=__stack_pointer",
        "-Wl,--export-table",
        "-DNDEBUG",
        "-fno-math-errno",
        "-I",
        $GLM_PATH,
        "-I",
        $WASM_RUNTIME_DIR,
        "-include", "wasm_freestanding.h"
    )

    # Adding Clang's resource headers at front
    $CLANG_RESOURCE_DIR="$(clang --print-resource-dir)\include"
    if ($CLANG_RESOURCE_DIR) {
        $clangArgs += "-I"; $clangArgs += "$CLANG_RESOURCE_DIR"
    }

    if ($INCLUDE_STUBS) {
        $clangArgs += "-I"; $clangArgs += $INCLUDE_STUBS
    }

    # Add libc++ and clang builtin includes
    $clangArgs += "-I"; $clangArgs += $libcxxInclude
    if ($clangBuiltinInclude) { $clangArgs += "-I"; $clangArgs += $clangBuiltinInclude }

    # Add emscripten C headers (musl) so internal headers like bits/* are available
    $emscriptenCandidates = @()
    $emsRelPaths = @("system/lib/libc/musl/arch/emscripten","system/lib/libc/musl/include","system/lib/libc/musl/arch/generic","system/lib/libc/musl/src/include","system/lib/libc/musl/src/internal","system/local/include","system/include","system/include/bits")
    foreach ($rel in $emsRelPaths) {
        $p = Join-Path $EMSCRIPTEN_ROOT $rel
        $emscriptenCandidates += $p
    }
    $addedEmscriptenIncludes = @()
    foreach ($p in $emscriptenCandidates) {
        if (Test-Path $p) { $clangArgs += "-I"; $clangArgs += $p; $addedEmscriptenIncludes += $p }
    }

    # Find a dlmalloc/malloc source file inside EMSCRIPTEN_ROOT if present and compile it together
    $mallocSrc = $null
    if (Test-Path $EMSCRIPTEN_ROOT) {
        $found = Get-ChildItem -Path $EMSCRIPTEN_ROOT -Recurse -Include "dlmalloc.c" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $mallocSrc = $found.FullName }
    }

    # Compile specific math sources from musl (freestanding math)
    if (Test-Path $EMSCRIPTEN_ROOT) {
        $muslMathDir = Join-Path $EMSCRIPTEN_ROOT "system/lib/libc/musl/src/math"
        # Common math functions for terrain generation (sin, cos, floor, sqrt, fmod) + dependencies
        $mathFiles = @("sin.c", "cos.c", "floor.c", "sqrt.c", "fmod.c", "__sin.c", "__cos.c", "__rem_pio2.c", "__rem_pio2_large.c", "scalbn.c", "copysign.c", "fabs.c")
        
        foreach ($file in $mathFiles) {
            $fullPath = Join-Path $muslMathDir $file
            if (Test-Path $fullPath) {
                Write-Host "Compiling math source: $file"
                $clangArgs += "-x"; $clangArgs += "c"; $clangArgs += $fullPath
            } else {
                Write-Host "Warning: Math source $file not found at $fullPath"
            }
        }
    }

    # Runtime source
    $runtimeSrc = Join-Path $WASM_RUNTIME_DIR "wasm_freestanding.cpp"

    # Output and input
    $clangArgs += "-o"; $clangArgs += $outputFile; $clangArgs += $inputFile
    if (Test-Path $runtimeSrc) { $clangArgs += $runtimeSrc }

    # Include malloc source if found
    if ($mallocSrc) {
        Write-Host "Found malloc source: $mallocSrc - compiling it into the module"
        $clangArgs += "-x"; $clangArgs += "c"; $clangArgs += $mallocSrc
    }

    # Debug: print resolved include paths and clang args so missing headers can be diagnosed
    Write-Host "Resolved include paths:" 
    if ($libcxxInclude) { Write-Host "  libc++:" $libcxxInclude }
    if ($clangBuiltinInclude) { Write-Host "  clang-builtin:" $clangBuiltinInclude }
    Write-Host "Additional includes:" $GLM_PATH
    if ($emmallocSrc) { Write-Host "emmalloc source:" $emmallocSrc }
    Write-Host "Invoking: $CLANG`nArgs: $($clangArgs -join ' ')"

    & $CLANG @clangArgs
}

Write-Host "WASM Build Complete."
