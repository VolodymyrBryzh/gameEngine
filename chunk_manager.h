#pragma once
#include "chunk.h"
#include "tree_assets.h"
#include <unordered_map>
#include <cstdint>
#include <cmath>

class ChunkManager {
    const PerlinNoise& pn;
    const TreeAssets*  treeAssets = nullptr;
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

    void SetTreeAssets(const TreeAssets& assets) { treeAssets = &assets; }

    // Apply cylinder collision against any tree trunk in the 3×3 player vicinity.
    // Pushes pos out radially. Player can pass over fallen trees / above the trunk top.
    void ApplyTreeCollision(Vector3& pos, float playerR = 0.30f) const {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                auto it = chunks.find(Key(pcx + dx, pcz + dz));
                if (it == chunks.end()) continue;
                for (const auto& t : it->second.trees) {
                    if (t.fallen) continue;
                    if (pos.y > t.pos.y + t.trunkH) continue;   // above trunk → no hit
                    float ddx = pos.x - t.pos.x;
                    float ddz = pos.z - t.pos.z;
                    float d2  = ddx*ddx + ddz*ddz;
                    float r   = t.trunkR + playerR;
                    if (d2 >= r*r) continue;
                    float d = sqrtf(d2);
                    if (d < 0.0001f) { d = 0.0001f; ddx = r; }  // edge case: exactly inside
                    float push = r - d;
                    pos.x += (ddx / d) * push;
                    pos.z += (ddz / d) * push;
                }
            }
    }

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
                if (t.fallen) continue;
                float dx = t.pos.x - playerPos.x;
                float dz = t.pos.z - playerPos.z;
                if (dx*dx + dz*dz > td2) continue;
                if (treeAssets) {
                    DrawModelEx(treeAssets->model, t.pos,
                                { 0, 1, 0 }, RAD2DEG * t.rotation,
                                { t.scale, t.scale, t.scale }, WHITE);
                }
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
