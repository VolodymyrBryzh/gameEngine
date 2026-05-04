#pragma once
#include "chunk.h"
#include <unordered_map>
#include <cstdint>
#include <cmath>

class ChunkManager {
    const PerlinNoise& pn;
    std::unordered_map<int64_t, Chunk> chunks;

    // Encode (cx, cz) as a single int64 key, handles negative coords
    static int64_t Key(int cx, int cz) {
        return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    }

    static int ChunkCoord(float worldPos) {
        return (int)floorf(worldPos / CHUNK_SIZE);
    }

public:
    int viewRadius = 4;  // load chunks within this radius (4 → 9×9 = 81 chunks)
    int keepRadius = 6;  // keep chunks in memory beyond view (avoid reload thrash)

    explicit ChunkManager(const PerlinNoise& pn) : pn(pn) {}

    // Synchronous bulk load around a position — call once before the game loop.
    void LoadImmediate(Vector3 pos, int radius = 2) {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        for (int dx = -radius; dx <= radius; dx++)
            for (int dz = -radius; dz <= radius; dz++) {
                int64_t k = Key(pcx + dx, pcz + dz);
                if (!chunks.count(k))
                    chunks.emplace(k, BuildChunk(pn, pcx + dx, pcz + dz));
            }
    }

    // Per-frame streaming: load up to maxNew missing chunks, unload distant ones.
    void Update(Vector3 pos, int maxNew = 3) {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        int generated = 0;

        for (int dx = -viewRadius; dx <= viewRadius && generated < maxNew; dx++)
            for (int dz = -viewRadius; dz <= viewRadius && generated < maxNew; dz++) {
                int64_t k = Key(pcx + dx, pcz + dz);
                if (!chunks.count(k)) {
                    chunks.emplace(k, BuildChunk(pn, pcx + dx, pcz + dz));
                    generated++;
                }
            }

        // Unload chunks beyond keepRadius
        for (auto it = chunks.begin(); it != chunks.end(); ) {
            Chunk& c = it->second;
            if (abs(c.cx - pcx) > keepRadius || abs(c.cz - pcz) > keepRadius) {
                c.Unload();
                it = chunks.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Height at any world position — sampled directly (fast enough for one call/frame)
    float GetHeight(float x, float z) const {
        return SampleWorldHeight(pn, x, z);
    }

    // Draw all ready chunks + visible objects within render distances
    void Render(Vector3 playerPos, float treeDist, float rockDist) const {
        const float td2 = treeDist * treeDist;
        const float rd2 = rockDist * rockDist;

        for (const auto& [k, c] : chunks) {
            if (!c.ready) continue;

            DrawModel(c.model, { 0, 0, 0 }, 1.0f, WHITE);

            for (const auto& t : c.trees) {
                float dx = t.pos.x - playerPos.x;
                float dz = t.pos.z - playerPos.z;
                if (dx*dx + dz*dz > td2) continue;
                DrawCylinder({ t.pos.x, t.pos.y,            t.pos.z }, 0.2f,     0.15f, t.trunkH,        6, BROWN);
                DrawCylinder({ t.pos.x, t.pos.y + t.trunkH, t.pos.z }, t.crownR, 0.0f,  t.crownR * 1.4f, 7, DARKGREEN);
            }

            for (const auto& r : c.rocks) {
                float dx = r.pos.x - playerPos.x;
                float dz = r.pos.z - playerPos.z;
                if (dx*dx + dz*dz > rd2) continue;
                DrawSphere({ r.pos.x, r.pos.y + r.radius * 0.6f, r.pos.z }, r.radius, DARKGRAY);
            }
        }
    }

    int ChunkCount() const { return (int)chunks.size(); }
};
