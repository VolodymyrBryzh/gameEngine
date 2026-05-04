# Survival Engine

> A first-person survival prototype built in C++17 with [raylib 5.0](https://www.raylib.com/) — infinite procedurally generated world, real-time chunk streaming, and handcrafted tree geometry.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square&logo=c%2B%2B)
![raylib 5.0](https://img.shields.io/badge/raylib-5.0-orange?style=flat-square)
![CMake](https://img.shields.io/badge/CMake-3.12%2B-green?style=flat-square&logo=cmake)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey?style=flat-square)

---

## Features

| Category | Details |
|---|---|
| **Terrain** | Domain-warped fBm Perlin noise — mountains, plains, beaches |
| **World** | Infinite chunk streaming — 32×32 unit chunks, loaded on the fly |
| **Objects** | Procedural trees (tapered trunk + teardrop crown) and rocks |
| **Physics** | Gravity, jumping, cylindrical trunk collision |
| **Camera** | Smooth first-person mouse-look with head-bob |
| **Rendering** | Flat-shaded vertex colors, baked Lambert lighting, water plane |

---

## World Generation

The terrain uses **domain warping**: two low-frequency fBm maps displace the input coordinates of the main height map, producing natural-looking ridges and valleys without obvious tiling.

```
warpX = fbm(x·0.005, z·0.005, 3) → shifts X sample point
warpZ = fbm(x·0.005+5.2, z·0.005+1.3, 3) → shifts Z sample point
height = fbm((x + warpX·25) · 0.01, (z + warpZ·25) · 0.01, 6) · 80
```

Height zones drive both color and object placement:

| Height | Color | Objects |
|---|---|---|
| < 12 | Sand (Beige) | — |
| 12 – 38 | Grass (Dark Green) | Trees, Rocks |
| 38 – 60 | Rock (Gray) | Rocks |
| > 60 | Snow (White) | — |

---

## Chunk System

The world is divided into 32×32 unit chunks managed by `ChunkManager`:

```
viewRadius = 4  →  9×9  = 81 active chunks   (~165k triangles)
keepRadius = 6  →  13×13 = 169 chunks max in memory
maxNew     = 3  →  up to 3 new chunks generated per frame
```

Each chunk is a flat-shaded indexed mesh with its own tree and rock lists. Chunks are keyed by a packed `int64` (`(uint32)cx << 32 | (uint32)cz`) for fast O(1) lookup.

---

## Tree Geometry

Every tree is a single shared mesh drawn via `DrawModelEx` with per-instance scale and Y-rotation:

- **Trunk** — 8-sided tapered cylinder, `r` narrows from 0.20 at the base to 0.13 at the top
- **Crown** — teardrop ellipsoid: `r(v) = R · sin(π·v)^0.5 · (0.65 + 0.35·v)`, wider at the bottom
- **Bottom cap** — triangle fan sealing the underside so the crown is solid from all angles

Collision is a simple cylinder test run against all trees in the 3×3 surrounding chunks each frame.

---

## Building

**Requirements:** CMake 3.12+, a C++17 compiler (GCC / MinGW / MSVC), internet access for the first build (CMake fetches raylib automatically).

```bash
# 1. Clone
git clone https://github.com/VolodymyrBryzh/gameEngine.git
cd gameEngine

# 2. Configure
cmake -B build -G "MinGW Makefiles"   # Windows + MinGW
# cmake -B build                      # Windows + MSVC / Linux / macOS

# 3. Build
cmake --build build

# 4. Run
./build/game.exe
```

> Raylib 5.0 is fetched automatically on first configure — no manual setup needed.

---

## Controls

| Key | Action |
|---|---|
| `W A S D` | Move |
| `Mouse` | Look around |
| `Shift` | Sprint (15 km/h) |
| `Space` | Jump |
| `Esc` | Quit |

---

## Project Structure

```
GameEngine/
├── main.cpp            — game loop, player physics, camera
├── chunk.h             — Chunk struct, BuildChunk(), SampleWorldHeight()
├── chunk_manager.h     — streaming, collision, rendering
├── tree_assets.h       — procedural tree mesh (shared, GPU-side)
├── perlin_noise.h      — Perlin noise + fBm implementation
├── CMakeLists.txt      — build config, auto-fetches raylib
└── ROADMAP.md          — planned features
```

---

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full plan. Short version:

- [ ] Configurable seed + random world start
- [ ] Chunk seam fix (sample beyond chunk borders for smooth normals)
- [ ] Async chunk generation (worker threads, no frame stutter)
- [ ] LOD — low-res meshes for distant chunks
- [ ] Persistent world changes (chopped trees saved as chunk deltas)
- [ ] Biomes — temperature/moisture maps drive height curve and object sets

---

## License

MIT — do whatever you want with it.
