**List of new features and other TODO items, of varying complexity:**

1. Add right click context menu in world selection menu for editing options (delete, rename, etc.)
2. Add audio engine and game sounds
3. Figure out how to distribute program to users
4. Learn about and figure out how to implement client-server structure to my program. This will likely be a huge project and involve big changes to my code, but I would like to be able to eventually play my "game" with my college roommates
5. Add WASM-based user definable terrain generation. This will consist of a JSON metadata file and a WASM code file, and the user should be able to choose from a list of terrain generators when creating a world which will include both built-in options and user-defined options
6. Persistent player data stored in JSON format inside each world. This is so that a player spawns where they left off last time they played a world, instead of going through the respawn procedure each time they join a world
7. Block selection via a "pick block" key and a history of previous blocks that have been picked
8. Survival and creative game modes. For survival, will have to implement an inventory of some kind, display the amount of a block when picked to be used/placed. Also, no flying in survival.
9. For survival mode, figure out crafting system
10. For creative mode, figure out how to select blocks that do not actually generate in the world (the user should not have to craft blocks to be able to pick them in creative mode). At the same time, though, I would like to avoid extensive use of a GUI, since this is apparently a design choice I have made.

**List of bugs to fix**

1. Wireframe mesh remains on screen sometimes when quitting back to the main menu from inside a world

**It is also important to spend more time testing my program so that I can find more bugs.**