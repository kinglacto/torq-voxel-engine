#pragma once

#include "PerlinNoise.hpp"

using seed_t = siv::PerlinNoise::seed_type;

class Noise {
public:
    seed_t seed;
    double frequency;
    double fx, fy;
    uint32_t octaves;
    siv::PerlinNoise noise;

    Noise(seed_t _seed, double _frequency, uint32_t _octaves, size_t size_x, size_t size_y);
    Noise() = default;
    float get2D(double x, double y);
};
