#ifndef PERLIN_NOISE_H
#define PERLIN_NOISE_H

#include <vector>
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

class PerlinNoise {
    std::vector<int> p;
public:
    PerlinNoise() {
        p.resize(256);
        std::iota(p.begin(), p.end(), 0);
        std::default_random_engine engine(12345); // Фіксований seed для стабільного ландшафту
        std::shuffle(p.begin(), p.end(), engine);
        p.insert(p.end(), p.begin(), p.end());
    }

    float noise(float x, float y) const {
        int X = (int)floor(x) & 255;
        int Y = (int)floor(y) & 255;

        x -= floor(x);
        y -= floor(y);

        float u = fade(x);
        float v = fade(y);

        int aa = p[p[X] + Y];
        int ab = p[p[X] + Y + 1];
        int ba = p[p[X + 1] + Y];
        int bb = p[p[X + 1] + Y + 1];

        float res = lerp(v, lerp(u, grad(p[aa], x, y), grad(p[ba], x - 1, y)),
                            lerp(u, grad(p[ab], x, y - 1), grad(p[bb], x - 1, y - 1)));
        return (res + 1.0f) / 2.0f;
    }

    // Fractal Brownian Motion - комбінує кілька шарів шуму
    float fbm(float x, float y, int octaves) const {
        float total = 0.0f;
        float persistence = 0.5f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;
        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }
        return total / maxValue;
    }

private:
    float fade(float t) const { return t * t * t * (t * (t * 6 - 15) + 10); }
    float lerp(float t, float a, float b) const { return a + t * (b - a); }
    float grad(int hash, float x, float y) const {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : h == 12 || h == 14 ? x : 0;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
};

#endif
