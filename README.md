# Install instructions (Windows)

1. Locate the desired release number on the GitHub repository page (pick the latest one unless you have good reason not to).
2. Download the x64 ZIP file for Windows, and extract its contents.
3. Take the directory contained within the ZIP file and move it wherever you please.
4. Inside this directory, there should be a `bin\` subdirectory containing the `SimplyVoxels.exe` executable. Run this executable to start the program. It will open up a terminal window, in addition to the main program window.

# Build from source instructions (using VS Code)

1. First, try F5 to build and run
2. If the build returns with non-code errors (i.e. CMake or dependency related or the like), try Ctrl + Shift + P and run the CMake: Clean Rebuild task before using F5 to run the program
3. If this still doesn't work, try deleting the build/ directory and repeating step 2
4. Another thing to check is your compiler version. On Windows, I used Visual Studio Community 2022 Release - x86_amd64 (17.9.4). On Linux, I used GCC 12.3.0 x86_64-linux-gnu. Both of these compilers worked for me on their respective platforms (Windows 11 and Ubuntu 22.04).
5. If everything is still broken, then IDK, I can't help you. Unless you are me, then I'll have to figure something out.