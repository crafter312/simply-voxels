**List of new features and other TODO items, of varying complexity:**

1. Add audio engine and game sounds
2. Add AppImage distributable file for Linux users
3. Learn about and figure out how to implement client-server structure to my program. This will likely be a huge project and involve big changes to my code, but I would like to be able to eventually play my "game" with my college roommates
4. Persistent player data stored in JSON format inside each world. This is so that a player spawns where they left off last time they played a world, instead of going through the respawn procedure each time they join a world
5. Block selection via a "pick block" key and a history of previous blocks that have been picked
6. Survival and creative game modes. For survival, will have to implement an inventory of some kind, display the amount of a block when picked to be used/placed. Also, no flying in survival.
7. For survival mode, figure out crafting system
8. For creative mode, figure out how to select blocks that do not actually generate in the world (the user should not have to craft blocks to be able to pick them in creative mode). At the same time, though, I would like to avoid extensive use of a GUI, since this is apparently a design choice I have made.
9. Rework `SaveGameManager` interactions to accept a `size_t` index of the world in the list, instead of a directory name. This will eliminate a search operation that needs to happen, and simplify the `UIManager` member variables and logic a tiny bit.
10. Add GUI warning screen for when the generator ID in a world is empty, giving the user a chance to abort if desired.
11. Add verbosity level for custom debug output macro `util/DebugLog.hpp` (mostly to separate the per frame output from everything else).

**List of bugs to fix**

1. Wireframe mesh remains on screen sometimes when quitting back to the main menu from inside a world
2. Game crashes when minimized (something about zero window size or something, I think)
3. Fix chunk boarder issue with WASM-based terrain generators (if you make a new world, generate some chunks, close the world, and then rejoin and generate more chunks, you'll see a noticable chunk boundary). Well, either this, or new chunks refuse to generate entirely. In the latter case, freshly deactivated regions seem to enter some kind of loop where they are continuously "added to queue for compaction." At this point, I have not done enough testing to really understand what is going on here.

**It is also important to spend more time testing my program so that I can find more bugs.**