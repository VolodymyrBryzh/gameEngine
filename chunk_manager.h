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
#include <cmath>

class ChunkManager {
    const PerlinNoise& pn;
    const TreeAssets*  treeAssets = nullptr;
    std::unordered_map<int64_t, Chunk> chunks;

    // Chunks currently queued or being built — prevents double-queueing.
    // Accessed only on the main thread, no mutex needed.
    std::unordered_set<int64_t> inFlight;

    // Work queue: (cx,cz) pairs waiting for a worker thread
    std::queue<std::pair<int,int>> workQueue;
    std::mutex                     workMutex;
    std::condition_variable        workCV;

    // Upload queue: CPU-ready results waiting for main-thread GPU upload
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

    void WorkerLoop() {
        while (true) {
            std::pair<int,int> task;
            {
                std::unique_lock<std::mutex> lk(workMutex);
                workCV.wait(lk, [&]{ return stopFlag.load() || !workQueue.empty(); });
                if (stopFlag && workQueue.empty()) return;
                task = workQueue.front();
                workQueue.pop();
            }
            ChunkBuildResult r = BuildChunkCPU(pn, task.first, task.second);
            {
                std::lock_guard<std::mutex> lk(uploadMutex);
                uploadQueue.push(std::move(r));
            }
        }
    }

public:
    int viewRadius = 4;
    int keepRadius = 6;

    explicit ChunkManager(const PerlinNoise& pn, int numWorkers = 2) : pn(pn) {
        for (int i = 0; i < numWorkers; i++)
            workers.emplace_back(&ChunkManager::WorkerLoop, this);
    }

    // Must be called before CloseWindow() so Unload() has a valid GL context.
    void Shutdown() {
        {
            std::lock_guard<std::mutex> lk(workMutex);
            stopFlag = true;
        }
        workCV.notify_all();
        for (auto& w : workers) w.join();
        workers.clear();

        // Free CPU buffers for results that never reached the GPU
        {
            std::lock_guard<std::mutex> lk(uploadMutex);
            while (!uploadQueue.empty()) {
                ChunkBuildResult& r = uploadQueue.front();
                MemFree(r.mesh.vertices);
                MemFree(r.mesh.normals);
                MemFree(r.mesh.colors);
                uploadQueue.pop();
            }
        }

        for (auto& [k, c] : chunks) c.Unload();
        chunks.clear();
    }

    void SetTreeAssets(const TreeAssets& assets) { treeAssets = &assets; }

    void ApplyTreeCollision(Vector3& pos, float playerR = 0.30f) const {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                auto it = chunks.find(Key(pcx + dx, pcz + dz));
                if (it == chunks.end()) continue;
                for (const auto& t : it->second.trees) {
                    if (t.fallen) continue;
                    if (pos.y > t.pos.y + t.trunkH) continue;
                    float ddx = pos.x - t.pos.x;
                    float ddz = pos.z - t.pos.z;
                    float d2  = ddx*ddx + ddz*ddz;
                    float r   = t.trunkR + playerR;
                    if (d2 >= r*r) continue;
                    float d = sqrtf(d2);
                    if (d < 0.0001f) { d = 0.0001f; ddx = r; }
                    float push = r - d;
                    pos.x += (ddx / d) * push;
                    pos.z += (ddz / d) * push;
                }
            }
    }

    // Synchronous bulk load — called once before the game loop.
    void LoadImmediate(Vector3 pos, int radius = 2) {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);
        for (int dx = -radius; dx <= radius; dx++)
            for (int dz = -radius; dz <= radius; dz++) {
                int64_t k = Key(pcx + dx, pcz + dz);
                if (!chunks.count(k)) {
                    chunks.emplace(k, BuildChunk(pn, pcx + dx, pcz + dz));
                    inFlight.insert(k);
                }
            }
    }

    // Per-frame: drain upload queue (GPU, main thread), queue missing chunks, unload distant.
    void Update(Vector3 pos, int maxUpload = 2) {
        int pcx = ChunkCoord(pos.x);
        int pcz = ChunkCoord(pos.z);

        // 1. Upload CPU-ready results (limited per frame to avoid hitching)
        {
            std::lock_guard<std::mutex> lk(uploadMutex);
            int uploaded = 0;
            while (!uploadQueue.empty() && uploaded < maxUpload) {
                ChunkBuildResult r = std::move(uploadQueue.front());
                uploadQueue.pop();
                int64_t k = Key(r.cx, r.cz);
                inFlight.erase(k);
                if (!chunks.count(k)) {
                    Chunk c;
                    c.cx    = r.cx;
                    c.cz    = r.cz;
                    c.mesh  = r.mesh;
                    c.trees = std::move(r.trees);
                    c.rocks = std::move(r.rocks);
                    UploadMesh(&c.mesh, false);
                    c.model = LoadModelFromMesh(c.mesh);
                    c.ready = true;
                    chunks.emplace(k, std::move(c));
                } else {
                    MemFree(r.mesh.vertices);
                    MemFree(r.mesh.normals);
                    MemFree(r.mesh.colors);
                }
                uploaded++;
            }
        }

        // 2. Queue missing chunks in view radius
        {
            std::lock_guard<std::mutex> lk(workMutex);
            for (int dx = -viewRadius; dx <= viewRadius; dx++)
                for (int dz = -viewRadius; dz <= viewRadius; dz++) {
                    int64_t k = Key(pcx + dx, pcz + dz);
                    if (!chunks.count(k) && !inFlight.count(k)) {
                        inFlight.insert(k);
                        workQueue.push({pcx + dx, pcz + dz});
                    }
                }
            workCV.notify_all();
        }

        // 3. Unload distant chunks
        for (auto it = chunks.begin(); it != chunks.end(); ) {
            Chunk& c = it->second;
            if (abs(c.cx - pcx) > keepRadius || abs(c.cz - pcz) > keepRadius) {
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
            DrawModel(c.model, { 0, 0, 0 }, 1.0f, WHITE);

            for (const auto& t : c.trees) {
                if (t.fallen) continue;
                float dx = t.pos.x - playerPos.x;
                float dz = t.pos.z - playerPos.z;
                if (dx*dx + dz*dz > td2) continue;
                if (treeAssets)
                    DrawModelEx(treeAssets->model, t.pos,
                                { 0,1,0 }, RAD2DEG * t.rotation,
                                { t.scale, t.scale, t.scale }, WHITE);
            }

            for (const auto& r : c.rocks) {
                float dx = r.pos.x - playerPos.x;
                float dz = r.pos.z - playerPos.z;
                if (dx*dx + dz*dz > rd2) continue;
                DrawSphere({ r.pos.x, r.pos.y + r.radius * 0.6f, r.pos.z },
                           r.radius, DARKGRAY);
            }
        }
    }

    int ChunkCount() const { return (int)chunks.size(); }
};
