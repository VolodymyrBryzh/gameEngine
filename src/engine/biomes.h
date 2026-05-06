#pragma once
#include "raylib.h"
#include "perlin_noise.h"

enum BiomeType { FOREST, DESERT, TUNDRA, MOUNTAINS, SWAMP };
enum TreeType { OAK, SPRUCE, CACTUS };

struct BiomeParams {
    float heightScale;
    float heightOffset;
    float noiseFreq;
    Color color;
    float treeDensity;
    float rockDensity;
};

inline BiomeType GetBiome(const PerlinNoise& pn, float x, float z) {
    // Large-scale noise for temperature and moisture
    float temp  = pn.noise(x * 0.0004f + 500.0f, z * 0.0004f + 500.0f);
    float moist = pn.noise(x * 0.0004f - 500.0f, z * 0.0004f - 500.0f);

    if (temp < 0.35f) {
        if (moist < 0.5f) return TUNDRA;
        else return MOUNTAINS;
    }
    if (temp > 0.65f) {
        if (moist < 0.4f) return DESERT;
        if (moist > 0.6f) return SWAMP;
    }
    return FOREST;
}

inline BiomeParams GetBiomeParams(BiomeType type) {
    switch (type) {
        case DESERT:    return { 0.2f, 5.0f,  0.5f, { 237, 201, 175, 255 }, 0.05f, 0.2f }; // Sandy, flat
        case TUNDRA:    return { 0.4f, 10.0f, 0.8f, { 220, 240, 255, 255 }, 0.2f,  0.1f }; // Snowy, mid
        case MOUNTAINS: return { 2.5f, 20.0f, 1.2f, { 120, 120, 130, 255 }, 0.1f,  0.6f }; // Rocky, high
        case SWAMP:     return { 0.1f, 2.0f,  0.4f, { 40, 60, 30, 255 },    0.4f,  0.0f }; // Dark, very flat
        case FOREST:
        default:        return { 1.0f, 12.0f, 1.0f, { 34, 139, 34, 255 },   0.6f,  0.2f }; // Classic green
    }
}
