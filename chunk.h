#pragma once
#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include <vector>
#include <cmath>

constexpr int CHUNK_SIZE  = 32;
constexpr int CHUNK_VERTS = CHUNK_SIZE + 1;  // 33×33 vertices per chunk

struct TreeData { Vector3 pos; float trunkH; float crownR; };
struct RockData  { Vector3 pos; float radius; };

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

// Build a complete chunk at grid position (cx, cz).
// World coverage: x in [cx*32, cx*32+32], z in [cz*32, cz*32+32].
// Must be called from the main (OpenGL) thread.
inline Chunk BuildChunk(const PerlinNoise& pn, int cx, int cz) {
    Chunk c;
    c.cx = cx;
    c.cz = cz;

    const float ox = (float)(cx * CHUNK_SIZE);
    const float oz = (float)(cz * CHUNK_SIZE);

    // Precompute CHUNK_VERTS×CHUNK_VERTS height grid (stack, ~4 KB)
    float hmap[CHUNK_VERTS * CHUNK_VERTS];
    for (int i = 0; i < CHUNK_VERTS; i++)
        for (int j = 0; j < CHUNK_VERTS; j++)
            hmap[i * CHUNK_VERTS + j] = SampleWorldHeight(pn, ox + i, oz + j);

    auto H = [&](int i, int j) { return hmap[i * CHUNK_VERTS + j]; };

    // Build mesh (flat-shaded, indexed by triangle)
    Mesh& mesh = c.mesh;
    mesh.triangleCount = CHUNK_SIZE * CHUNK_SIZE * 2;
    mesh.vertexCount   = mesh.triangleCount * 3;
    mesh.vertices = (float *)        MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals  = (float *)        MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.colors   = (unsigned char *)MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char));

    int vi = 0;
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float wx = ox + x, wz = oz + z;
            Vector3 v1 = { wx,     H(x,   z  ), wz     };
            Vector3 v2 = { wx + 1, H(x+1, z  ), wz     };
            Vector3 v3 = { wx,     H(x,   z+1), wz + 1 };
            Vector3 v4 = { wx + 1, H(x+1, z+1), wz + 1 };

            auto AddTri = [&](Vector3 p1, Vector3 p2, Vector3 p3) {
                Vector3 n = Vector3Normalize(Vector3CrossProduct(
                    Vector3Subtract(p2, p1), Vector3Subtract(p3, p1)));
                Vector3 pts[3] = { p1, p2, p3 };
                for (int i = 0; i < 3; i++) {
                    mesh.vertices[vi*3]   = pts[i].x;
                    mesh.vertices[vi*3+1] = pts[i].y;
                    mesh.vertices[vi*3+2] = pts[i].z;
                    mesh.normals[vi*3]   = n.x;
                    mesh.normals[vi*3+1] = n.y;
                    mesh.normals[vi*3+2] = n.z;

                    float light = Vector3DotProduct(n, Vector3Normalize({ 0.5f, 1.0f, 0.2f }));
                    if (light < 0.3f) light = 0.3f;

                    Color col = DARKGREEN;
                    if      (pts[i].y > 60.0f) col = WHITE;
                    else if (pts[i].y > 40.0f) col = GRAY;
                    else if (pts[i].y < 12.0f) col = BEIGE;

                    mesh.colors[vi*4]   = (unsigned char)(col.r * light);
                    mesh.colors[vi*4+1] = (unsigned char)(col.g * light);
                    mesh.colors[vi*4+2] = (unsigned char)(col.b * light);
                    mesh.colors[vi*4+3] = 255;
                    vi++;
                }
            };

            AddTri(v1, v3, v2);
            AddTri(v2, v3, v4);
        }
    }

    UploadMesh(&mesh, false);
    c.model = LoadModelFromMesh(mesh);

    // Procedural object placement via jittered grid
    const int GRID = 7;
    for (int gx = 0; gx < CHUNK_SIZE; gx += GRID) {
        for (int gz = 0; gz < CHUNK_SIZE; gz += GRID) {
            float density = pn.noise((ox + gx) * 0.08f, (oz + gz) * 0.08f);
            float jx = (pn.noise((ox+gx) * 0.4f, (oz+gz) * 0.1f) - 0.5f) * GRID * 0.9f;
            float jz = (pn.noise((ox+gx) * 0.1f, (oz+gz) * 0.4f) - 0.5f) * GRID * 0.9f;
            float px = ox + gx + jx;
            float pz = oz + gz + jz;
            float h  = SampleWorldHeight(pn, px, pz);

            // Trees: green zone only (height 12..38)
            if (h >= 12.0f && h <= 38.0f && density > 0.55f) {
                float s = 0.7f + pn.noise(px * 0.2f, pz * 0.2f) * 0.6f;
                c.trees.push_back({ {px, h, pz}, 2.5f * s, 2.8f * s });
            }
            // Rocks: anywhere above water, less frequent
            else if (h > 8.0f && h <= 55.0f && density < 0.18f) {
                float s = 0.4f + pn.noise(px * 0.3f, pz * 0.3f) * 0.5f;
                c.rocks.push_back({ {px, h, pz}, 0.5f * s });
            }
        }
    }

    c.ready = true;
    return c;
}
