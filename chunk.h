#pragma once
#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include <vector>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <filesystem>

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

struct ChunkBuildResult {
    int cx = 0, cz = 0, lod = 0;
    Mesh mesh = {};
    std::vector<TreeData> trees;
    std::vector<RockData> rocks;
};

struct Chunk {
    int cx = 0, cz = 0, lod = 0;
    Mesh  mesh  = {};
    Model model = {};
    std::vector<TreeData> trees;
    std::vector<RockData> rocks;
    bool ready = false;
    bool dirty = false;

    void Unload() {
        if (ready) { UnloadModel(model); ready = false; }
    }
};

inline float SampleWorldHeight(const PerlinNoise& pn, float x, float z) {
    float warpX = pn.fbm(x * 0.005f,        z * 0.005f,        3) * 2.0f - 1.0f;
    float warpZ = pn.fbm(x * 0.005f + 5.2f, z * 0.005f + 1.3f, 3) * 2.0f - 1.0f;
    const float W = 25.0f;
    return pn.fbm((x + warpX * W) * 0.01f, (z + warpZ * W) * 0.01f, 6) * 80.0f;
}

inline std::string GetChunkDeltaPath(int cx, int cz) {
    return "save/chunks/" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
}

inline void SaveChunkDelta(int cx, int cz, const std::vector<TreeData>& trees) {
    std::filesystem::create_directories("save/chunks");
    std::ofstream f(GetChunkDeltaPath(cx, cz), std::ios::binary);
    if (!f.is_open()) return;

    uint32_t magic = 0x544C4443; // 'CDLT'
    uint32_t ver   = 1;
    
    std::vector<uint32_t> dirtyIndices;
    for (size_t i = 0; i < trees.size(); i++) {
        // We consider it "changed" if hp < 100 or fallen. 
        // This is a simple heuristic since base generation always has hp=100, fallen=false.
        if (trees[i].hp < 100 || trees[i].fallen) {
            dirtyIndices.push_back((uint32_t)i);
        }
    }

    uint32_t count = (uint32_t)dirtyIndices.size();
    f.write((char*)&magic, 4);
    f.write((char*)&ver,   4);
    f.write((char*)&count, 4);

    for (uint32_t idx : dirtyIndices) {
        f.write((char*)&idx, 4);
        f.write((char*)&trees[idx].hp, 4);
        uint8_t fallen = trees[idx].fallen ? 1 : 0;
        f.write((char*)&fallen, 1);
    }
}

inline void LoadChunkDelta(int cx, int cz, std::vector<TreeData>& trees) {
    std::ifstream f(GetChunkDeltaPath(cx, cz), std::ios::binary);
    if (!f.is_open()) return;

    uint32_t magic, ver, count;
    f.read((char*)&magic, 4);
    if (magic != 0x544C4443) return;
    f.read((char*)&ver, 4);
    f.read((char*)&count, 4);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx;
        int hp;
        uint8_t fallen;
        f.read((char*)&idx, 4);
        f.read((char*)&hp, 4);
        f.read((char*)&fallen, 1);
        if (idx < trees.size()) {
            trees[idx].hp = hp;
            trees[idx].fallen = (fallen != 0);
        }
    }
}

// CPU-only build — safe to call from worker threads.
// lod=0: full 33×33 grid + objects; lod=1: 17×17 grid, no objects (4× fewer triangles).
inline ChunkBuildResult BuildChunkCPU(const PerlinNoise& pn, int cx, int cz, int lod = 0) {
    ChunkBuildResult r;
    r.cx  = cx;
    r.cz  = cz;
    r.lod = lod;

    const float ox = (float)(cx * CHUNK_SIZE);
    const float oz = (float)(cz * CHUNK_SIZE);
    const int   step = (lod == 0) ? 1 : 2;

    // Extended heightmap: covers [-2, CHUNK_SIZE+2] so central-difference normals
    // are correct at borders for both step=1 and step=2.
    constexpr int HW = CHUNK_SIZE + 5;  // 37×37
    float hmap[HW * HW];
    for (int i = 0; i < HW; i++)
        for (int j = 0; j < HW; j++)
            hmap[i * HW + j] = SampleWorldHeight(pn, ox + i - 2, oz + j - 2);

    // H(i,j) with i,j in [-2, CHUNK_SIZE+2]
    auto H = [&](int i, int j) { return hmap[(i+2) * HW + (j+2)]; };

    const Vector3 kLight = Vector3Normalize({ 0.5f, 1.0f, 0.2f });
    // Central differences scaled by step so the normal magnitude is independent of LOD.
    auto SmoothN = [&](int i, int j) -> Vector3 {
        float fs = (float)step;
        return Vector3Normalize({
            H(i-step, j) - H(i+step, j),
            2.0f * fs,
            H(i, j-step) - H(i, j+step)
        });
    };
    auto VertColor = [](float y) -> Color {
        if      (y > 60.0f) return WHITE;
        else if (y > 40.0f) return GRAY;
        else if (y < 12.0f) return BEIGE;
        else                return DARKGREEN;
    };

    int cells = CHUNK_SIZE / step;          // 32 or 16
    // Skirt: cells × 8 extra triangles (4 edges × cells × 2 tris) hide LOD seams
    int skirtTris = cells * 8;
    Mesh& mesh = r.mesh;
    mesh.triangleCount = cells * cells * 2 + skirtTris;
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

    for (int x = 0; x < CHUNK_SIZE; x += step)
        for (int z = 0; z < CHUNK_SIZE; z += step) {
            AddV(x,        z       ); AddV(x,        z+step); AddV(x+step, z      );
            AddV(x+step,   z       ); AddV(x,        z+step); AddV(x+step, z+step );
        }

    // === SKIRTS ===
    // Vertical strips going down by SKIRT_DEPTH around the chunk perimeter.
    // They hide gaps where LOD=0 (33 verts/edge) meets LOD=1 (17 verts/edge):
    // the LOD=0 detail bumps create a vertical mismatch with LOD=1's straight line,
    // and the skirt's downward face covers that gap.
    constexpr float SKIRT_DEPTH = 8.0f;
    auto AddSkirtTri = [&](Vector3 p1, Vector3 p2, Vector3 p3, Color base) {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(
            Vector3Subtract(p2, p1), Vector3Subtract(p3, p1)));
        float lit = Vector3DotProduct(n, kLight);
        if (lit < 0.3f) lit = 0.3f;
        Vector3 pts[3] = { p1, p2, p3 };
        for (int k = 0; k < 3; k++) {
            mesh.vertices[vi*3]   = pts[k].x;
            mesh.vertices[vi*3+1] = pts[k].y;
            mesh.vertices[vi*3+2] = pts[k].z;
            mesh.normals[vi*3]    = n.x;
            mesh.normals[vi*3+1]  = n.y;
            mesh.normals[vi*3+2]  = n.z;
            mesh.colors[vi*4]     = (unsigned char)(base.r * lit);
            mesh.colors[vi*4+1]   = (unsigned char)(base.g * lit);
            mesh.colors[vi*4+2]   = (unsigned char)(base.b * lit);
            mesh.colors[vi*4+3]   = 255;
            vi++;
        }
    };
    auto SkirtQuad = [&](int gx1, int gz1, int gx2, int gz2, bool flip) {
        Vector3 a    = { ox + gx1, H(gx1, gz1),                oz + gz1 };
        Vector3 b    = { ox + gx2, H(gx2, gz2),                oz + gz2 };
        Vector3 aBot = { a.x,      a.y - SKIRT_DEPTH,          a.z      };
        Vector3 bBot = { b.x,      b.y - SKIRT_DEPTH,          b.z      };
        Color  col   = VertColor((a.y + b.y) * 0.5f);
        if (flip) { AddSkirtTri(a, aBot, bBot, col); AddSkirtTri(a, bBot, b,    col); }
        else      { AddSkirtTri(a, bBot, aBot, col); AddSkirtTri(a, b,    bBot, col); }
    };

    for (int x = 0; x < CHUNK_SIZE; x += step) {
        SkirtQuad(x, 0,          x + step, 0,          false);  // south, outward -Z
        SkirtQuad(x, CHUNK_SIZE, x + step, CHUNK_SIZE, true);   // north, outward +Z
    }
    for (int z = 0; z < CHUNK_SIZE; z += step) {
        SkirtQuad(0,          z, 0,          z + step, true);   // west,  outward -X
        SkirtQuad(CHUNK_SIZE, z, CHUNK_SIZE, z + step, false);  // east,  outward +X
    }

    // Objects only on full-resolution chunks
    if (lod == 0) {
        const int GRID = 7;
        for (int gx = 0; gx < CHUNK_SIZE; gx += GRID) {
            for (int gz = 0; gz < CHUNK_SIZE; gz += GRID) {
                float density = pn.noise((ox+gx) * 0.08f, (oz+gz) * 0.08f);
                float jx = (pn.noise((ox+gx)*0.4f, (oz+gz)*0.1f) - 0.5f) * GRID * 0.9f;
                float jz = (pn.noise((ox+gx)*0.1f, (oz+gz)*0.4f) - 0.5f) * GRID * 0.9f;
                float px = ox + gx + jx;
                float pz = oz + gz + jz;
                float h  = SampleWorldHeight(pn, px, pz);

                if (h >= 12.0f && h <= 38.0f && density > 0.55f) {
                    float scale = 0.7f + pn.noise(px*0.2f, pz*0.2f) * 0.8f;
                    float rot   = pn.noise(px*0.5f, pz*0.7f) * 2.0f * PI;
                    r.trees.push_back({ {px,h,pz}, scale, rot,
                        3.0f*scale, 0.20f*scale, 100, false });
                } else if (h > 8.0f && h <= 55.0f && density < 0.18f) {
                    float s = 0.4f + pn.noise(px*0.3f, pz*0.3f) * 0.5f;
                    r.rocks.push_back({ {px,h,pz}, 0.5f*s });
                }
            }
        }
        LoadChunkDelta(cx, cz, r.trees);
    }

    return r;
}

// GPU upload — main thread only.
inline Chunk BuildChunk(const PerlinNoise& pn, int cx, int cz, int lod = 0) {
    ChunkBuildResult r = BuildChunkCPU(pn, cx, cz, lod);
    Chunk c;
    c.cx    = r.cx;
    c.cz    = r.cz;
    c.lod   = r.lod;
    c.mesh  = r.mesh;
    c.trees = std::move(r.trees);
    c.rocks = std::move(r.rocks);
    UploadMesh(&c.mesh, false);
    c.model = LoadModelFromMesh(c.mesh);
    c.ready = true;
    return c;
}
