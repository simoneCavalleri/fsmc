/**
 * @file sha256.hpp
 * @brief Zero-dependency FIPS 180-4 SHA-256 implementation for deterministic IR and manifest hashing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fsm::ir {

namespace detail {

[[nodiscard]] constexpr std::uint32_t rotr32(std::uint32_t x, std::uint32_t n) noexcept {
    return (x >> n) | (x << (32U - n));
}

inline constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

inline void sha256_transform(std::array<std::uint32_t, 8>& state, const std::uint8_t block[64]) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24U) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16U) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8U) | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i - 15], 7U) ^ rotr32(w[i - 15], 18U) ^ (w[i - 15] >> 3U);
        const std::uint32_t s1 = rotr32(w[i - 2], 17U) ^ rotr32(w[i - 2], 19U) ^ (w[i - 2] >> 10U);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
        const std::uint32_t S0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace detail

/**
 * @brief Computes the lowercase 64-character hex SHA-256 digest of @p data (FIPS 180-4).
 */
[[nodiscard]] inline std::string sha256_hex(std::string_view data) {
    std::array<std::uint32_t, 8> state = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                          0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    std::uint8_t block[64]{};
    std::size_t offset = 0;
    const std::size_t len = data.size();

    while (offset + 64 <= len) {
        for (std::size_t i = 0; i < 64; ++i) {
            block[i] = static_cast<std::uint8_t>(data[offset + i]);
        }
        detail::sha256_transform(state, block);
        offset += 64;
    }

    const std::size_t rem = (len - offset) & 63U;
    for (std::size_t i = 0; i < rem; ++i) {
        block[i] = static_cast<std::uint8_t>(data[offset + i]);
    }
    block[rem] = 0x80U;
    if (rem >= 56) {
        for (std::size_t i = rem + 1; i < 64; ++i) {
            block[i] = 0;
        }
        detail::sha256_transform(state, block);
        for (std::size_t i = 0; i < 56; ++i) {
            block[i] = 0;
        }
    } else {
        for (std::size_t i = rem + 1; i < 56; ++i) {
            block[i] = 0;
        }
    }

    const std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8ULL;
    for (std::size_t i = 0; i < 8; ++i) {
        block[56 + i] = static_cast<std::uint8_t>((bit_len >> (56U - i * 8U)) & 0xffU);
    }
    detail::sha256_transform(state, block);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex.push_back(kHex[(word >> static_cast<unsigned>(shift)) & 0x0fU]);
        }
    }
    return hex;
}

}  // namespace fsm::ir
