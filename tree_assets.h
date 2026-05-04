#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <cstring>

// Shared tree mesh used by every tree instance via DrawModelEx.
// Local coords: trunk from y=0 to y=TRUNK_H, crown from y=CROWN_Y to y=CROWN_Y+CROWN_H.
// Per-instance variation comes from scale + Y rotation.
struct TreeAssets {
    Mesh  mesh  = {};
    Model model = {};

    // Local-space dimensions — used by gameplay code (collision, fall pivot, etc.)
    static constexpr float TRUNK_H      = 3.0f;
    static constexpr float TRUNK_R_BASE = 0.20f;
    static constexpr float TRUNK_R_TOP  = 0.13f;
    static constexpr float CROWN_Y      = 2.4f;
    static constexpr float CROWN_H      = 2.6f;
    static constexpr float CROWN_R      = 1.20f;

    void Unload() { UnloadModel(model); }
};

inline TreeAssets BuildTreeMesh() {
    constexpr int TRUNK_SIDES  = 8;
    constexpr int CROWN_RINGS  = 7;   // rings along the egg's vertical axis
    constexpr int CROWN_SLICES = 12;  // segments around Y

    std::vector<float>         verts;
    std::vector<float>         norms;
    std::vector<unsigned char> cols;

    Vector3 lightDir = Vector3Normalize({ 0.5f, 1.0f, 0.2f });

    auto addV = [&](Vector3 p, Vector3 n, Color base) {
        float lit = Vector3DotProduct(n, lightDir);
        if (lit < 0.4f) lit = 0.4f;
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        norms.push_back(n.x); norms.push_back(n.y); norms.push_back(n.z);
        cols.push_back((unsigned char)(base.r * lit));
        cols.push_back((unsigned char)(base.g * lit));
        cols.push_back((unsigned char)(base.b * lit));
        cols.push_back(base.a);
    };

    auto addTri = [&](Vector3 a, Vector3 b, Vector3 c, Color base) {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(
            Vector3Subtract(b, a), Vector3Subtract(c, a)));
        addV(a, n, base);
        addV(b, n, base);
        addV(c, n, base);
    };

    // === TRUNK: tapered cylinder ===
    //   Bottom ring at y=0 with radius TRUNK_R_BASE; top ring at y=TRUNK_H with TRUNK_R_TOP.
    //   Side panel between two angles is split into two triangles.
    Color trunkCol = { 92, 64, 38, 255 };
    for (int i = 0; i < TRUNK_SIDES; i++) {
        float a1 = (float)(i)     / TRUNK_SIDES * 2.0f * PI;
        float a2 = (float)(i + 1) / TRUNK_SIDES * 2.0f * PI;
        Vector3 b1 = { cosf(a1) * TreeAssets::TRUNK_R_BASE, 0,                    sinf(a1) * TreeAssets::TRUNK_R_BASE };
        Vector3 b2 = { cosf(a2) * TreeAssets::TRUNK_R_BASE, 0,                    sinf(a2) * TreeAssets::TRUNK_R_BASE };
        Vector3 t1 = { cosf(a1) * TreeAssets::TRUNK_R_TOP,  TreeAssets::TRUNK_H,  sinf(a1) * TreeAssets::TRUNK_R_TOP  };
        Vector3 t2 = { cosf(a2) * TreeAssets::TRUNK_R_TOP,  TreeAssets::TRUNK_H,  sinf(a2) * TreeAssets::TRUNK_R_TOP  };
        addTri(b1, t1, t2, trunkCol);
        addTri(b1, t2, b2, trunkCol);
    }

    // === CROWN: teardrop egg ===
    //   r(v) = R · sin(π·v)^0.5 · (0.65 + 0.35·v)  — wider at bottom
    //   v=0 → top apex, v=1 → bottom apex
    Color crownCol = { 38, 92, 38, 255 };
    auto crownVert = [&](int ring, int slice) -> Vector3 {
        float v = (float)ring / CROWN_RINGS;
        float u = (float)slice / CROWN_SLICES * 2.0f * PI;
        float r = TreeAssets::CROWN_R * powf(sinf(PI * v), 0.5f) * (0.65f + 0.35f * v);
        float y = TreeAssets::CROWN_Y + TreeAssets::CROWN_H * (1.0f - v);
        return { r * cosf(u), y, r * sinf(u) };
    };
    for (int ring = 0; ring < CROWN_RINGS; ring++) {
        for (int slice = 0; slice < CROWN_SLICES; slice++) {
            int s2 = (slice + 1) % CROWN_SLICES;
            Vector3 a = crownVert(ring,     slice);
            Vector3 b = crownVert(ring,     s2);
            Vector3 c = crownVert(ring + 1, s2);
            Vector3 d = crownVert(ring + 1, slice);
            addTri(a, b, c, crownCol);
            addTri(a, c, d, crownCol);
        }
    }

    // === CROWN BOTTOM CAP: flat disk sealing the underside ===
    {
        Vector3 capCenter = { 0.0f, TreeAssets::CROWN_Y, 0.0f };
        Vector3 capNormal = { 0.0f, -1.0f, 0.0f };
        for (int slice = 0; slice < CROWN_SLICES; slice++) {
            int s2 = (slice + 1) % CROWN_SLICES;
            Vector3 a = crownVert(CROWN_RINGS - 1, slice);
            Vector3 b = crownVert(CROWN_RINGS - 1, s2);
            addV(capCenter, capNormal, crownCol);
            addV(a, capNormal, crownCol);
            addV(b, capNormal, crownCol);
        }
    }

    Mesh mesh = {};
    mesh.vertexCount   = (int)(verts.size() / 3);
    mesh.triangleCount = mesh.vertexCount / 3;
    mesh.vertices = (float *)        MemAlloc((unsigned int)(verts.size() * sizeof(float)));
    mesh.normals  = (float *)        MemAlloc((unsigned int)(norms.size() * sizeof(float)));
    mesh.colors   = (unsigned char *)MemAlloc((unsigned int)(cols.size()));
    memcpy(mesh.vertices, verts.data(), verts.size() * sizeof(float));
    memcpy(mesh.normals,  norms.data(), norms.size() * sizeof(float));
    memcpy(mesh.colors,   cols.data(),  cols.size());
    UploadMesh(&mesh, false);

    TreeAssets t;
    t.mesh  = mesh;
    t.model = LoadModelFromMesh(mesh);
    return t;
}
