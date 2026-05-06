#pragma once
#include "chunk.h"
#include "tree_assets.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <climits>
#include <cmath>

struct WorkItem { int cx, cz, lod; };

class ChunkManager {
    const PerlinNoise& pn;
    const TreeAssets*  treeAssets = nullptr;
    std::unordered_map<int64_t, Chunk> chunks;

    // key → lod currently queued/building; main-thread only, no mutex needed.
    std::unordered_map<int64_t, int> inFlight;

    std::queue<WorkItem>         workQueue;
    std::mutex                   workMutex;
    std::condition_variable      workCV;

    std::queue<ChunkBuildResult> uploadQueue;
    std::mutex                   uploadMutex;

    std::vector<std::thread> workers;
    std::atomic<bool>        stopFlag{false};

    static int64_t Key(int cx, int cz) {
        return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    }
    static int ChunkCoord(float worldPos) {
        return (int)floorf(worldPos / CHUNK_SIZE);
    }
    static int NeededLod(int dx, int dz, int lodRadius) {
        return (abs(dx) <= lodRadius && abs(dz) <= lodRadius) ? 0 : 1;
    }

    void WorkerLoop() {
        while (true) {
            WorkItem task;
            {
                std::unique_lock<std::mutex> lk(workMutex);
                workCV.wait(lk, [&]{ return stopFlag.load() || !workQueue.empty(); });
                if (stopFlag && workQueue.empty()) return;
                task = workQueue.front();
                workQueue.pop();
            }
            ChunkBuildResult r = BuildChunkCPU(pn, task.cx, task.cz, task.lod);
            {
                std::lock_guard<std::mutex> lk(uploadMutex);
                uploadQueue.push(std::move(r));
            }
        }
    }

public:
    int lodRadius  = 2;   // within this → LOD 0 (full 33×33 + objects)
    int viewRadius = 7;   // beyond lodRadius up to here → LOD 1 (17×17, no objects)
    int keepRadius = 9;

    explicit ChunkManager(const PerlinNoise& pn, int numWorkers = 2) : pn(pn) {
        for (int i = 0; i < numWorkers; i++)
            workers.emplace_back(&ChunkManager::WorkerLoop, this);
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lk(workMutex);
            stopFlag = true;
        }
        workCV.notify_all();
        for (auto& w : workers) w.join();
        workers.clear();

        {
            std::lock_guard<std::mutex> lk(uploadMutex);
            while (!uploadQueue.empty()) {
                auto& r = uploadQueue.front();
                MemFree(r.mesh.vertices);
                MemFree(r.mesh.normals);
                MemFree(r.mesh.colors);
                uploadQueue.pop();
            }
        }
        for (auto& [k, c] : chunks) {
            if (c.dirty) SaveChunkDelta(c.cx, c.cz, c.trees);
            c.Unload();
        }
        chunks.clear();
    }

    void SetTreeAssets(const TreeAssets& assets) { treeAssets = &assets; }

    void ApplyTreeCollision(Vector3& pos, float playerR = 0.30f) const {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                auto it = chunks.find(Key(pcx+dx, pcz+dz));
                if (it == chunks.end()) continue;
                for (const auto& t : it->second.trees) {
                    if (t.fallen) continue;
                    if (pos.y > t.pos.y + t.trunkH) continue;
                    float ddx = pos.x - t.pos.x, ddz = pos.z - t.pos.z;
                    float d2  = ddx*ddx + ddz*ddz;
                    float r   = t.trunkR + playerR;
                    if (d2 >= r*r) continue;
                    float d = sqrtf(d2);
                    if (d < 0.0001f) { d = 0.0001f; ddx = r; }
                    pos.x += (ddx/d) * (r-d);
                    pos.z += (ddz/d) * (r-d);
                }
            }
    }

    void LoadImmediate(Vector3 pos, int radius = 2) {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        for (int dx = -radius; dx <= radius; dx++)
            for (int dz = -radius; dz <= radius; dz++) {
                int64_t k = Key(pcx+dx, pcz+dz);
                if (!chunks.count(k)) {
                    int lod = NeededLod(dx, dz, lodRadius);
                    chunks.emplace(k, BuildChunk(pn, pcx+dx, pcz+dz, lod));
                    inFlight[k] = lod;
                }
            }
    }

    void Update(Vector3 pos, int maxUpload = 2) {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);

        auto MakeChunk = [](ChunkBuildResult&& r) -> Chunk {
            Chunk c;
            c.cx    = r.cx;  c.cz   = r.cz;  c.lod  = r.lod;
            c.mesh  = r.mesh;
            c.trees = std::move(r.trees);
            c.rocks = std::move(r.rocks);
            UploadMesh(&c.mesh, false);
            c.model = LoadModelFromMesh(c.mesh);
            c.ready = true;
            return c;
        };

        // 1. Upload CPU-ready results — swap on LOD upgrade, never leave a hole.
        // A new result is accepted if no chunk exists OR existing chunk has worse LOD
        // (lod number is larger). Same-or-better LOD: discard the result.
        {
            std::lock_guard<std::mutex> lk(uploadMutex);
            int uploaded = 0;
            while (!uploadQueue.empty() && uploaded < maxUpload) {
                ChunkBuildResult r = std::move(uploadQueue.front());
                uploadQueue.pop();
                int64_t k = Key(r.cx, r.cz);
                inFlight.erase(k);

                auto it = chunks.find(k);
                if (it == chunks.end()) {
                    chunks.emplace(k, MakeChunk(std::move(r)));
                } else if (r.lod < it->second.lod) {
                    it->second.Unload();
                    it->second = MakeChunk(std::move(r));
                } else {
                    MemFree(r.mesh.vertices);
                    MemFree(r.mesh.normals);
                    MemFree(r.mesh.colors);
                }
                uploaded++;
            }
        }

        // 2. Queue chunks that don't exist or are at a worse LOD than needed.
        // Existing higher-quality chunks (lod < needLod) are kept — no downgrades.
        {
            std::lock_guard<std::mutex> lk(workMutex);
            for (int dx = -viewRadius; dx <= viewRadius; dx++)
                for (int dz = -viewRadius; dz <= viewRadius; dz++) {
                    int64_t k       = Key(pcx+dx, pcz+dz);
                    int     needLod = NeededLod(dx, dz, lodRadius);
                    auto    it      = chunks.find(k);
                    int     curLod  = (it != chunks.end()) ? it->second.lod : INT_MAX;
                    if (curLod > needLod && !inFlight.count(k)) {
                        inFlight[k] = needLod;
                        workQueue.push({pcx+dx, pcz+dz, needLod});
                    }
                }
            workCV.notify_all();
        }

        // 3. Unload distant chunks
        for (auto it = chunks.begin(); it != chunks.end(); ) {
            Chunk& c = it->second;
            if (abs(c.cx-pcx) > keepRadius || abs(c.cz-pcz) > keepRadius) {
                if (c.dirty) SaveChunkDelta(c.cx, c.cz, c.trees);
                inFlight.erase(Key(c.cx, c.cz));
                c.Unload();
                it = chunks.erase(it);
            } else {
                ++it;
            }
        }
    }

    float GetHeight(float x, float z) const {
        return SampleWorldHeight(pn, x, z);
    }

    void Render(Vector3 playerPos, float treeDist, float rockDist) const {
        const float td2 = treeDist * treeDist;
        const float rd2 = rockDist * rockDist;

        for (const auto& [k, c] : chunks) {
            if (!c.ready) continue;
            DrawModel(c.model, { 0,0,0 }, 1.0f, WHITE);

            for (const auto& t : c.trees) {
                if (t.fallen) continue;
                float dx = t.pos.x - playerPos.x, dz = t.pos.z - playerPos.z;
                if (dx*dx + dz*dz > td2) continue;
                if (treeAssets)
                    DrawModelEx(treeAssets->model, t.pos,
                                {0,1,0}, RAD2DEG * t.rotation,
                                {t.scale,t.scale,t.scale}, WHITE);
            }

            for (const auto& r : c.rocks) {
                float dx = r.pos.x - playerPos.x, dz = r.pos.z - playerPos.z;
                if (dx*dx + dz*dz > rd2) continue;
                DrawSphere({r.pos.x, r.pos.y + r.radius*0.6f, r.pos.z}, r.radius, DARKGRAY);
            }
        }
    }

    int ChunkCount() const { return (int)chunks.size(); }

    bool TryHitTree(Vector3 origin, Vector3 dir, float maxDist = 4.0f) {
        float minDist = maxDist;
        TreeData* targetTree = nullptr;
        Chunk*    targetChunk = nullptr;

        for (auto& [k, c] : chunks) {
            if (c.lod != 0) continue;
            for (auto& t : c.trees) {
                if (t.fallen) continue;
                // Approximate tree as a vertical line segment from t.pos to t.pos.y + t.trunkH
                // and check distance from ray to it.
                Ray ray = { origin, dir };
                RayCollision col = GetRayCollisionSphere(ray, 
                    { t.pos.x, t.pos.y + t.trunkH * 0.5f, t.pos.z }, t.trunkH * 0.5f);
                
                if (col.hit && col.distance < minDist) {
                    minDist = col.distance;
                    targetTree = &t;
                    targetChunk = &c;
                }
            }
        }

        if (targetTree) {
            targetTree->hp -= 25;
            if (targetTree->hp <= 0) targetTree->fallen = true;
            targetChunk->dirty = true;
            return true;
        }
        return false;
    }
};
