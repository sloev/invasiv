// md5.hpp
// ---------------------------------------------------------------
// Header-only MD5 implementation (public domain)
// Source: https://github.com/Chocobo1/Hash/blob/master/src/md5.h
// ---------------------------------------------------------------
#ifndef MD5_HPP
#define MD5_HPP

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace md5_impl
{
    constexpr std::array<uint32_t, 64> k = { {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    } };

    constexpr std::array<uint32_t, 64> r = { {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    } };

    inline uint32_t leftRotate(uint32_t v, int s) noexcept
    {
        return (v << s) | (v >> (32 - s));
    }

    inline void transform(uint32_t state[4], const uint8_t block[64]) noexcept
    {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

        for (std::size_t i = 0; i < 64; ++i)
        {
            uint32_t f, g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }

            uint32_t temp = d;
            d = c;
            c = b;
            b = b + leftRotate(a + f + k[i] + block[g], r[i]);
            a = temp;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    }
} // namespace md5_impl

class MD5
{
public:
    MD5() noexcept { init(); }

    void init() noexcept
    {
        m_state = { {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} };
        m_bitCount = 0;
        m_buffer.fill(0);
    }

    void accumulate(const void* data, std::size_t size)
    {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        std::size_t idx = m_bitCount / 8 % 64;
        m_bitCount += static_cast<uint64_t>(size) * 8;

        std::size_t part = 64 - idx;
        std::size_t i = 0;

        if (size >= part)
        {
            std::copy(ptr, ptr + part, m_buffer.begin() + idx);
            md5_impl::transform(m_state.data(), m_buffer.data());

            for (i = part; i + 63 < size; i += 64)
                md5_impl::transform(m_state.data(), ptr + i);

            idx = 0;
        }
        else { i = 0; }

        std::copy(ptr + i, ptr + size, m_buffer.begin() + idx);
    }

    void accumulate_from_file(const std::string& filename)
    {
        std::ifstream f(filename, std::ios::binary);
        if (!f) return;

        char buf[8192];
        while (f.read(buf, sizeof(buf)))
            accumulate(buf, f.gcount());
        if (f.gcount())
            accumulate(buf, f.gcount());
    }

    std::array<uint8_t, 16> finalise()
    {
        std::size_t idx = m_bitCount / 8 % 64;
        m_buffer[idx++] = 0x80;

        if (idx > 56)
        {
            std::fill(m_buffer.begin() + idx, m_buffer.end(), 0);
            md5_impl::transform(m_state.data(), m_buffer.data());
            idx = 0;
        }

        std::fill(m_buffer.begin() + idx, m_buffer.end() - 8, 0);

        uint64_t bits = m_bitCount;
        for (int i = 0; i < 8; ++i)
            m_buffer[m_buffer.size() - 1 - i] = static_cast<uint8_t>(bits >> (i * 8));

        md5_impl::transform(m_state.data(), m_buffer.data());

        std::array<uint8_t, 16> digest{};
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t j = 0; j < 4; ++j)
                digest[i * 4 + j] = static_cast<uint8_t>((m_state[i] >> (j * 8)) & 0xFF);

        init();               // reset for next use
        return digest;
    }

private:
    std::array<uint32_t, 4> m_state;
    std::array<uint8_t, 64> m_buffer{};
    uint64_t m_bitCount = 0;
};

// ---------------------------------------------------------------------
// Helper – MD5 hex string for a file
// ---------------------------------------------------------------------
inline std::string compute_md5(const std::string& filepath)
{
    if (!std::filesystem::is_regular_file(filepath))
        return {};

    MD5 md5;
    md5.accumulate_from_file(filepath);
    auto digest = md5.finalise();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : digest)
        oss << std::setw(2) << static_cast<unsigned>(b);
    return oss.str();
}

#endif // MD5_HPP