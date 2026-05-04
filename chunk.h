#pragma once
#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include <vector>
#include <cmath>

constexpr int CHUNK_SIZE = 32;

struct TreeData {
    Vector3 pos;
    float   scale;
    float   rotation;
    float   trunkH;
    float   trunkR;
    int     hp;
    bool    fallen;
};
struct RockData { Vector3 pos; float radius; };

// Holds CPU-side mesh arrays + object lists before GPU upload.
// Safe to build on any thread — no OpenGL calls inside.
struct ChunkBuildResult {
    int cx = 0, cz = 0;
    Mesh mesh = {};
    std::vector<TreeData> trees;
    std::vector<RockData> rocks;
};

struct Chunk {
    int cx = 0, cz = 0;
    Mesh  mesh  = {};
    Model model = {};
    std::vector<TreeData> trees;
    std::vector<RockData> rocks;
    bool ready = false;

    void Unload() {
        if (ready) { UnloadModel(model); ready = false; }
    }
};

// Domain-warped height at any world (x, z). Deterministic, no caching.
inline float SampleWorldHeight(const PerlinNoise& pn, float x, float z) {
    float warpX = pn.fbm(x * 0.005f,        z * 0.005f,        3) * 2.0f - 1.0f;
    float warpZ = pn.fbm(x * 0.005f + 5.2f, z * 0.005f + 1.3f, 3) * 2.0f - 1.0f;
    const float W = 25.0f;
    return pn.fbm((x + warpX * W) * 0.01f, (z + warpZ * W) * 0.01f, 6) * 80.0f;
}

// CPU-only build — safe to call from worker threads.
// Fills mesh.vertices/normals/colors and object lists; does NOT call UploadMesh.
inline ChunkBuildResult BuildChunkCPU(const PerlinNoise& pn, int cx, int cz) {
    ChunkBuildResult r;
    r.cx = cx;
    r.cz = cz;

    const float ox = (float)(cx * CHUNK_SIZE);
    const float oz = (float)(cz * CHUNK_SIZE);

    constexpr int HW = CHUNK_SIZE + 3;  // 35×35 extended grid
    float hmap[HW * HW];
    for (int i = 0; i < HW; i++)
        for (int j = 0; j < HW; j++)
            hmap[i * HW + j] = SampleWorldHeight(pn, ox + i - 1, oz + j - 1);

    auto H = [&](int i, int j) { return hmap[(i+1) * HW + (j+1)]; };

    const Vector3 kLight = Vector3Normalize({ 0.5f, 1.0f, 0.2f });
    auto SmoothN = [&](int i, int j) -> Vector3 {
        return Vector3Normalize({ H(i-1,j) - H(i+1,j), 2.0f, H(i,j-1) - H(i,j+1) });
    };
    auto VertColor = [](float y) -> Color {
        if      (y > 60.0f) return WHITE;
        else if (y > 40.0f) return GRAY;
        else if (y < 12.0f) return BEIGE;
        else                return DARKGREEN;
    };

    Mesh& mesh = r.mesh;
    mesh.triangleCount = CHUNK_SIZE * CHUNK_SIZE * 2;
    mesh.vertexCount   = mesh.triangleCount * 3;
    mesh.vertices = (float *)        MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals  = (float *)        MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.colors   = (unsigned char *)MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char));

    int vi = 0;
    auto AddV = [&](int gx, int gz) {
        float wy    = H(gx, gz);
        Vector3 n   = SmoothN(gx, gz);
        float light = Vector3DotProduct(n, kLight);
        if (light < 0.3f) light = 0.3f;
        Color col = VertColor(wy);
        mesh.vertices[vi*3]   = ox + gx;
        mesh.vertices[vi*3+1] = wy;
        mesh.vertices[vi*3+2] = oz + gz;
        mesh.normals[vi*3]    = n.x;
        mesh.normals[vi*3+1]  = n.y;
        mesh.normals[vi*3+2]  = n.z;
        mesh.colors[vi*4]     = (unsigned char)(col.r * light);
        mesh.colors[vi*4+1]   = (unsigned char)(col.g * light);
        mesh.colors[vi*4+2]   = (unsigned char)(col.b * light);
        mesh.colors[vi*4+3]   = 255;
        vi++;
    };

    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            AddV(x,   z  ); AddV(x,   z+1); AddV(x+1, z  );
            AddV(x+1, z  ); AddV(x,   z+1); AddV(x+1, z+1);
        }

    const int GRID = 7;
    for (int gx = 0; gx < CHUNK_SIZE; gx += GRID) {
        for (int gz = 0; gz < CHUNK_SIZE; gz += GRID) {
            float density = pn.noise((ox + gx) * 0.08f, (oz + gz) * 0.08f);
            float jx = (pn.noise((ox+gx) * 0.4f, (oz+gz) * 0.1f) - 0.5f) * GRID * 0.9f;
            float jz = (pn.noise((ox+gx) * 0.1f, (oz+gz) * 0.4f) - 0.5f) * GRID * 0.9f;
            float px = ox + gx + jx;
            float pz = oz + gz + jz;
            float h  = SampleWorldHeight(pn, px, pz);

            if (h >= 12.0f && h <= 38.0f && density > 0.55f) {
                float scale = 0.7f + pn.noise(px * 0.2f, pz * 0.2f) * 0.8f;
                float rot   = pn.noise(px * 0.5f, pz * 0.7f) * 2.0f * PI;
                r.trees.push_back({ {px,h,pz}, scale, rot,
                    3.0f*scale, 0.20f*scale, 100, false });
            } else if (h > 8.0f && h <= 55.0f && density < 0.18f) {
                float s = 0.4f + pn.noise(px * 0.3f, pz * 0.3f) * 0.5f;
                r.rocks.push_back({ {px,h,pz}, 0.5f*s });
            }
        }
    }

    return r;
}

// GPU upload — main thread only.
inline Chunk BuildChunk(const PerlinNoise& pn, int cx, int cz) {
    ChunkBuildResult r = BuildChunkCPU(pn, cx, cz);
    Chunk c;
    c.cx    = r.cx;
    c.cz    = r.cz;
    c.mesh  = r.mesh;
    c.trees = std::move(r.trees);
    c.rocks = std::move(r.rocks);
    UploadMesh(&c.mesh, false);
    c.model = LoadModelFromMesh(c.mesh);
    c.ready = true;
    return c;
}
