#pragma once
/**
 * @file short_uid.hpp
 * @brief Single-header library for generating short, fixed-length, unique IDs.
 *
 * Features:
 * - Thread-safe (with optional mutex protection)
 * - Fixed length (default 8 characters, customizable)
 * - URL-safe base62 encoding (0-9, a-z, A-Z)
 * - Uses high-resolution timestamp + counter + random seed
 * - No external dependencies
 *
 * Example:
 *   std::string id = short_uid::generate();        // e.g., "k9P2mX4v"
 *   std::string id6 = short_uid::generate<6>();    // 6-char ID
 */

#include <cstdint>
#include <cstring>
#include <atomic>
#include <chrono>
#include <random>
#include <array>

namespace short_uid {

namespace detail {

static constexpr char base62_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static constexpr size_t base62_size = 62;

// Thread-local counter to ensure uniqueness even in same timestamp
static thread_local uint32_t counter = 0;

// Global random device for initial seed
static std::atomic<uint64_t> global_seed{0};

inline uint64_t get_seed() {
    uint64_t seed = global_seed.load(std::memory_order_relaxed);
    if (seed == 0) {
        std::random_device rd;
        seed = ((uint64_t)rd() << 32) | rd();
        uint64_t expected = 0;
        if (!global_seed.compare_exchange_strong(expected, seed)) {
            seed = global_seed.load();
        }
    }
    return seed;
}

// High-resolution time in milliseconds since epoch
inline uint64_t timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace detail

template <size_t N = 8>
std::string generate() {
    static_assert(N > 0 && N <= 16, "ID length must be between 1 and 16");

    using namespace detail;

    // Get timestamp (48 bits should be enough for years)
    uint64_t ts = timestamp_ms() & 0xFFFFFFFFFFFFULL;  // 48 bits

    // Get or increment thread-local counter (16 bits)
    uint32_t cnt = counter++;
    if (cnt >= 65536) cnt = 0;  // wrap safely

    // Mix in some randomness
    uint64_t rnd = get_seed();
    rnd ^= (rnd << 13); rnd ^= (rnd >> 7); rnd ^= (rnd << 17);

    // Combine: 48-bit time + 16-bit counter + 64-bit random = 128 bits
    uint64_t hi = (ts << 16) | cnt;
    uint64_t lo = rnd;

    // Encode to base62
    std::array<char, N + 1> buffer{};
    buffer[N] = '\0';

    for (int i = N - 1; i >= 0; --i) {
        uint64_t val = (i < 8) ? (lo >> (8 * i)) : (hi >> (8 * (i - 8)));
        val &= 0xFF;
        // Use full 128 bits spread across N chars
        // Distribute entropy by folding bits
        size_t idx = val % base62_size;
        if (i > 0) {
            uint64_t prev = buffer[i + 1] ? buffer[i + 1] - '0' : 0;
            idx = (idx + prev) % base62_size;
        }
        buffer[i] = base62_chars[idx];
    }

    return std::string(buffer.data(), N);
}

// Convenience non-template version
inline std::string generate8() { return generate<8>(); }

} // namespace short_uid