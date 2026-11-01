#pragma once

#include <cmath>
#include <cstdint>

namespace drift
{
/** xoshiro256** con siembra splitmix64: rápido, determinista y reproducible por semilla. */
class Rng
{
public:
    explicit Rng (uint64_t seed) noexcept
    {
        uint64_t x = seed;
        for (auto& s : state)
            s = splitmix (x);
    }

    uint64_t next() noexcept
    {
        const uint64_t result = rotl (state[1] * 5, 7) * 9;
        const uint64_t t = state[1] << 17;
        state[2] ^= state[0]; state[3] ^= state[1];
        state[1] ^= state[2]; state[0] ^= state[3];
        state[2] ^= t;
        state[3] = rotl (state[3], 45);
        return result;
    }

    /** [0,1) */
    float uniform() noexcept { return (float) (next() >> 40) * (1.0f / 16777216.0f); }
    float uniform (float lo, float hi) noexcept { return lo + (hi - lo) * uniform(); }
    /** Triangular centrada: más probable el centro del rango, nunca sale de él. */
    float triangular (float lo, float hi) noexcept { return lo + (hi - lo) * 0.5f * (uniform() + uniform()); }
    int below (int n) noexcept { return n <= 1 ? 0 : (int) (uniform() * (float) n) % n; }
    bool chance (float p) noexcept { return uniform() < p; }

    int pickWeighted (const float* w, int n) noexcept
    {
        float total = 0.0f;
        for (int i = 0; i < n; ++i) total += w[i] > 0.0f ? w[i] : 0.0f;
        if (total <= 0.0f) return -1;
        float r = uniform() * total;
        for (int i = 0; i < n; ++i)
        {
            const float wi = w[i] > 0.0f ? w[i] : 0.0f;
            if (r < wi) return i;
            r -= wi;
        }
        for (int i = n - 1; i >= 0; --i) if (w[i] > 0.0f) return i;
        return -1;
    }

    static uint64_t splitmix (uint64_t& x) noexcept
    {
        uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

private:
    static uint64_t rotl (uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }
    uint64_t state[4] {};
};
} // namespace drift
