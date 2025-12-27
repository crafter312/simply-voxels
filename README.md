# Install instructions (Windows)

1. Locate the desired release number on the GitHub repository page (pick the latest one unless you have good reason not to).
2. Download the x64 ZIP file for Windows, and extract its contents.
3. Take the directory contained within the ZIP file and move it wherever you please.
4. Inside this directory, there should be a `bin\` subdirectory containing the `SimplyVoxels.exe` executable. Run this executable to start the program. It will open up a terminal window, in addition to the main program window.

# Build from source instructions (using VS Code)

1. Download this GitHub repository with `git clone <LINK HERE>`, where `<LINK HERE>` is either the HTTPS or the SSH link to the repository.
2. Open VS Code, find the option to open a folder, navigate to the `simply-voxels` folder just downloaded in the previous step, and open the folder as a project. 
3. First, try F5 to build and run
4. If the build returns with non-code errors (i.e. CMake or dependency related or the like), try Ctrl + Shift + P and run the CMake: Clean Rebuild task before using F5 to run the program
5. If this still doesn't work, try deleting the build/ directory and repeating step 2
6. Another thing to check is your compiler version. On Windows, I used Visual Studio Community 2022 Release - x86_amd64 (17.9.4). On Linux, I used GCC 12.3.0 x86_64-linux-gnu. Both of these compilers worked for me on their respective platforms (Windows 11 and Ubuntu 22.04).
7. If everything is still broken, then IDK, I can't help you. Unless you are me, then I'll have to figure something out. Or unless you think my instructions are garbage, in which case you could help fix them.

# Build WASM terrain generator in development environment (currently Windows only)

1. First, follow `Build from source instructions (using VS Code)` to set up a development environment
2. Using `SineHills.cpp` as an example, create your own terrain generator. The function must match the format `void generate(i32, i32, i32, i64, i32)`. The first three arguments are the chunk coordinate, the fourth argument is the seed, and the last argument is a pointer to the memory where the `int16_t` block IDs will be stored. Note that the program reads a fixed number of CHUNK_VOLUME (currently 16\*16\*16, or 4096) block IDs from this memory, so it is expected that the user defined `generate` function write the same number of block IDs to this memory.
3. Type `Ctrl + R` and run the `Build WASM Generators (Windows)` task to compile your generator.
4. The output file will be located in `resources/terrain_generators/`. Create a subdirectory for your generator, move the relevant `.wasm` file into this directory, and rename the file to `generator.wasm`.
5. Also in your new subdirectory, add a JSON file containing the following text information: `name`, `author`, `version`, and `description`. This `JSON` file should be called `metadata.json`
6. When you go to create a new world, this new terrain generator _should_ show up in the dropdown menu as an option, with the information from the metadata file also displayed.

# Build WASM terrain generator in other environment

1. Use the above instructions for `Build WASM terrain gneerator in development environment (currently Windows only)` as a starting point.
2. Also, see the build script `scripts/build_wasm.ps1` and the corresponding task in `.vscode/tasks.json` for additional detailed guidance on how to actually build your WASM files.