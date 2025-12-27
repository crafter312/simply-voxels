# WASM Build Script using wasi-sdk in Reactor Mode
# This creates library-style WASM modules that export functions without requiring a main()

# Configuration
$WASI_SDK = "C:/wasi-sdk"
$GLM_PATH = "./build/_deps/glm-src"
$SRC_DIR = "./src/world/terrain/wasm"
$OUT_DIR = "./resources/terrain_generators"

# Compiler paths
$CLANG = Join-Path $WASI_SDK "bin/clang++.exe"
$SYSROOT = Join-Path $WASI_SDK "share/wasi-sysroot"

# Validate wasi-sdk installation
if (!(Test-Path $CLANG)) {
    Write-Host "Error: clang++ not found at $CLANG"
    Write-Host "Please install wasi-sdk to C:/wasi-sdk or update the script."
    exit 1
}

if (!(Test-Path $SYSROOT)) {
    Write-Host "Error: wasi-sysroot not found at $SYSROOT"
    exit 1
}

# Ensure output directory exists
if (!(Test-Path $OUT_DIR)) {
    New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null
}

# Compile each .cpp file in the source directory
Get-ChildItem -Path $SRC_DIR -Filter *.cpp | ForEach-Object {
    $inputFile = $_.FullName
    $outputFile = Join-Path $OUT_DIR ($_.BaseName + ".wasm")

    Write-Host "Compiling $($_.Name) to WASM using wasi-sdk..."

    $clangArgs = @(
        # Target and sysroot
        "--target=wasm32-wasi",
        "--sysroot=$SYSROOT",

        # Reactor mode: library-style module, no main() required
        "-mexec-model=reactor",

        # Optimization
        "-O3",
        "-flto",

        # C++ settings
        "-fno-exceptions",
        "-std=c++17",

        # Export the generate function and memory
        "-Wl,--export=generate",
        "-Wl,--export=__wasm_call_ctors",

        # Reduce unnecessary imports
        "-DNDEBUG",
        "-fno-math-errno",

        # GLM configuration
        "-DGLM_FORCE_PURE",

        # Include paths
        "-I", $GLM_PATH,

        # Output
        "-o", $outputFile,
        $inputFile
    )

    Write-Host "Invoking: $CLANG"
    Write-Host "Args: $($clangArgs -join ' ')"

    & $CLANG @clangArgs

    if ($LASTEXITCODE -eq 0) {
        Write-Host "Successfully compiled: $outputFile" -ForegroundColor Green

        # Show file size
        $fileSize = (Get-Item $outputFile).Length
        Write-Host "Output size: $([math]::Round($fileSize / 1024, 2)) KB"
    } else {
        Write-Host "Failed to compile: $($_.Name)" -ForegroundColor Red
    }

    Write-Host ""
}

Write-Host "WASM Build Complete."