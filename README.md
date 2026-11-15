# Highway Endless

**Highway Endless** is a 3D arcade highway racing game developed from scratch in **C++** using **Modern OpenGL**, **GLFW**, **GLEW**, and **Miniaudio**. Players navigate an infinitely generated highway, dodge dynamic traffic, collect coins, customize vehicles, and compete in local split-screen battles.

---

## Features

- **Single Player Mode**: Endless procedural road generation with progressive difficulty, dynamic traffic spawning, weather effects, and Time Warp slow-motion mechanics.
- **2 Player Battle Mode**: Local split-screen multiplayer where two players compete simultaneously to be the first to reach the 3000-meter finish line.
- **Garage & Customization**:
  - 6 unlockable vehicles (*Street Fox*, *Crimson GT*, *Vortex R*, *Phantom V8*, *Apex X*, *Hyperion Z*) with distinct Speed, Grip, and Armor stats.
  - 4 upgradeable components: Engine, Tires, Armor, and Magnet.
  - Custom vehicle paint selection.
- **Progression & Persistence**: In-game achievement system (8 milestones), XP/leveling progression, coin economy, and persistent local high-score saving.
- **Custom Graphics & Audio Pipeline**: Real-time 3D rendering in OpenGL with dynamic lighting, headlights, rear-view mirror, and procedural engine/SFX audio via `miniaudio`.

---

## Controls

### Single Player
| Key | Action |
| :--- | :--- |
| `A` / `D` or `Left` / `Right` | Change Lanes |
| `W` or `Up` | Accelerate |
| `S` or `Down` | Brake |
| `Shift` | Activate Nitro Boost |
| `Q` | Activate Time Warp |
| `P` | Pause Game |
| `R` | Instant Restart (on Crash / Game Over) |
| `ESC` | Return to Main Menu |

### 2 Player Battle (Split-Screen)
- **Player 1 (Left Screen)**: `W A S D` to drive, `Left Shift` for Nitro.
- **Player 2 (Right Screen)**: `Arrow Keys` to drive, `Right Ctrl` for Nitro.

---

## Tech Stack

- **Language**: C++17
- **Graphics API**: OpenGL 3.3+
- **Window & Input**: GLFW
- **OpenGL Extension Loading**: GLEW
- **Audio Engine**: Miniaudio (`miniaudio.h`)
- **IDE / Build System**: Microsoft Visual Studio (`.sln` / `.vcxproj`)

---

## Project Structure

```text
Highway-Endless/
├── main.cpp          # Entry point, GLFW window initialization, OpenGL context, main loop
├── Game.cpp / .hpp   # Core gameplay logic, physics, AABB collision detection, state machine
├── Renderer.cpp/.hpp # 3D OpenGL rendering pipeline, lighting, weather, car models, HUD
├── Audio.cpp / .hpp  # Sound effects, engine RPM audio, and ambient audio via miniaudio
└── packages.config   # NuGet dependencies (GLFW / GLEW)
