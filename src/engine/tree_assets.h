#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <cstring>
#include "biomes.h"



// Shared tree meshes used by tree instances via DrawModelEx.
struct TreeAssets {
    Model oak;
    Model spruce;
    Model cactus;

    static constexpr float TRUNK_H      = 3.0f;
    static constexpr float TRUNK_R_BASE = 0.20f;
    static constexpr float TRUNK_R_TOP  = 0.13f;
    static constexpr float CROWN_Y      = 2.4f;
    static constexpr float CROWN_H      = 2.6f;
    static constexpr float CROWN_R      = 1.20f;

    void Unload() { 
        UnloadModel(oak); 
        UnloadModel(spruce); 
        UnloadModel(cactus); 
    }
};

inline Mesh BuildMesh(TreeType type) {
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
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        addV(a, n, base); addV(b, n, base); addV(c, n, base);
    };

    if (type == CACTUS) {
        Color cactCol = { 50, 150, 50, 255 };
        for (int i = 0; i < 6; i++) {
            float a1 = (float)i / 6 * 2.0f * PI, a2 = (float)(i+1) / 6 * 2.0f * PI;
            Vector3 b1 = {cosf(a1)*0.25f, 0, sinf(a1)*0.25f}, b2 = {cosf(a2)*0.25f, 0, sinf(a2)*0.25f};
            Vector3 t1 = {cosf(a1)*0.25f, 2.5f, sinf(a1)*0.25f}, t2 = {cosf(a2)*0.25f, 2.5f, sinf(a2)*0.25f};
            addTri(b1, t1, t2, cactCol); addTri(b1, t2, b2, cactCol);
            addTri({0, 2.7f, 0}, t1, t2, cactCol);
        }
    } else {
        Color trunkCol = { 92, 64, 38, 255 };
        for (int i = 0; i < 8; i++) {
            float a1 = (float)i/8 * 2.0f * PI, a2 = (float)(i+1)/8 * 2.0f * PI;
            Vector3 b1 = {cosf(a1)*0.2f, 0, sinf(a1)*0.2f}, b2 = {cosf(a2)*0.2f, 0, sinf(a2)*0.2f};
            Vector3 t1 = {cosf(a1)*0.13f, 3.0f, sinf(a1)*0.13f}, t2 = {cosf(a2)*0.13f, 3.0f, sinf(a2)*0.13f};
            addTri(b1, t1, t2, trunkCol); addTri(b1, t2, b2, trunkCol);
        }
        Color crownCol = (type == SPRUCE) ? Color{ 30, 60, 30, 255 } : Color{ 38, 92, 38, 255 };
        if (type == SPRUCE) {
            for (int r = 0; r < 3; r++) {
                float h = 1.5f + r * 1.0f, rad = 1.5f - r * 0.4f;
                for (int i = 0; i < 8; i++) {
                    float a1 = (float)i/8 * 2.0f * PI, a2 = (float)(i+1)/8 * 2.0f * PI;
                    Vector3 b1 = {cosf(a1)*rad, h, sinf(a1)*rad}, b2 = {cosf(a2)*rad, h, sinf(a2)*rad};
                    addTri({0, h+2.0f, 0}, b1, b2, crownCol);
                }
            }
        } else { // OAK
            auto crownVert = [&](int ring, int slice) -> Vector3 {
                float v = (float)ring/7, u = (float)slice/12 * 2.0f * PI;
                float r = 1.2f * powf(sinf(PI*v), 0.5f) * (0.65f + 0.35f*v);
                return { r*cosf(u), 2.4f + 2.6f*(1.0f-v), r*sinf(u) };
            };
            for (int r = 0; r < 7; r++) for (int s = 0; s < 12; s++) {
                int s2 = (s+1)%12;
                addTri(crownVert(r,s), crownVert(r,s2), crownVert(r+1,s2), crownCol);
                addTri(crownVert(r,s), crownVert(r+1,s2), crownVert(r+1,s), crownCol);
            }
        }
    }

    Mesh mesh = { 0 };
    memset(&mesh, 0, sizeof(Mesh)); // Ensure absolute zero for all fields (VBOs, etc)
    
    mesh.vertexCount = (int)verts.size()/3;
    mesh.triangleCount = mesh.vertexCount/3;
    mesh.vertices = (float*)MemAlloc((unsigned int)(verts.size()*sizeof(float)));
    mesh.normals  = (float*)MemAlloc((unsigned int)(norms.size()*sizeof(float)));
    
    if (cols.size() > 0) {
        mesh.colors = (unsigned char*)MemAlloc((unsigned int)cols.size());
        memcpy(mesh.colors, cols.data(), cols.size());
    }

    memcpy(mesh.vertices, verts.data(), verts.size()*sizeof(float));
    memcpy(mesh.normals, norms.data(), norms.size()*sizeof(float));
    
    UploadMesh(&mesh, false);
    return mesh;
}

inline TreeAssets BuildTreeMesh() {
    TreeAssets t;
    t.oak    = LoadModelFromMesh(BuildMesh(OAK));
    t.spruce = LoadModelFromMesh(BuildMesh(SPRUCE));
    t.cactus = LoadModelFromMesh(BuildMesh(CACTUS));
    return t;
}
