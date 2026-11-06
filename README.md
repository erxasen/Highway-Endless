Highway Endless
An arcade 3D racing game in C++ and OpenGL.
Dodge traffic, collect coins, chain combos and survive as long as you can.

About:
You drive a blue sports car down a three-lane highway. Weave through
traffic, grab coins, pull off near misses and keep your combo alive.
The longer you survive, the denser the traffic gets.

 Features:
-  Three lanes with smooth lane changes and car body roll
-  Four traffic types: sedan, sports car, van and truck
-  Coin lines of 3-6 coins
-  Combo system: coins and near misses raise your multiplier (up to x15)
-  Nitro boost that recharges on its own and is topped up by coins
-  Health bar and a short invulnerability window after a crash
-  Difficulty that ramps up over time
-  Day/night cycle, fog, trees, street lamps and city skyline
-  Custom vector-font HUD, no external fonts or textures
-  High score saved between runs

Controls:
- A / Left Arrow - change lane left
- D / Right Arrow - change lane right
- W / Up Arrow - accelerate
- S / Down Arrow - brake
- Shift - nitro boost
- P - pause / resume
- R - restart
- Esc - quit

Scoring and damage:
-  Coin: +120
-  Overtaken car: +60
-  Near miss: +180 and up
-  Crash damage: sedan and sports car 34, van 42, truck 55
-  Combo resets after about 3 seconds without a bonus, or on a crash
-  Nitro needs speed above 20 and recharges while not in use
-  Difficulty peaks after roughly 4 minutes

Building

Requirements:
- Windows
- Visual Studio 2022 (toolset v143) with the Desktop development with C++ workload
- GLFW 3 (glfw.org)
- GLEW (glew.sourceforge.net)

Steps:
1. Open Final project.sln in Visual Studio.
2. In the project properties, add GLFW and GLEW:
   - C/C++ > General > Additional Include Directories: the include folders of GLFW and GLEW
   - Linker > General > Additional Library Directories: the folders with the .lib files
   - Linker > Input > Additional Dependencies: glfw3.lib; glew32.lib; opengl32.lib
3. If you use dynamic GLEW, put glew32.dll next to the built .exe.
4. Pick a configuration (for example Debug | x64) and press Ctrl+Shift+B.

Project structure
- Final project.sln - Visual Studio solution
- Final project.vcxproj - project file
- main.cpp - entry point, GLFW window, main loop
- Game.hpp / Game.cpp - game logic, input, traffic, collisions
- Renderer.hpp / Renderer.cpp - scene, cars, road, HUD and font

Game holds all the logic and state. Renderer only reads that state
through const getters and draws it.

High score:
The best score is stored in highscore.txt in the working directory.
It is loaded at startup and saved on exit and on game over.
Delete the file to reset it.

Tuning

- Speeds: maxSpeed_ and baseCruiseSpeed_ in Game.hpp
- Difficulty and spawn rate: Game::updateDifficulty
- Crash damage: Game::checkCollisions
- Coin and near-miss bonuses: Game::updatePickups, Game::awardNearMiss
- Traffic colors: kTrafficColors in Game.cpp
