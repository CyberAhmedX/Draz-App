#include "draz/crypto.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace draz::crypto {

namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void compress(std::uint32_t state[8], const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
               (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t ch = (e & f) ^ (~e & g);
        std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// Streaming SHA-256 state, so HMAC can reuse the hashed key blocks.
struct Sha256 {
    std::uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t buffer[64] = {};
    std::size_t buffered = 0;
    std::uint64_t total = 0;

    void update(const std::uint8_t* data, std::size_t size) {
        total += size;
        while (size) {
            std::size_t take = 64 - buffered < size ? 64 - buffered : size;
            std::memcpy(buffer + buffered, data, take);
            buffered += take;
            data += take;
            size -= take;
            if (buffered == 64) {
                compress(state, buffer);
                buffered = 0;
            }
        }
    }

    // Padding: a 1 bit, zeros, then the message length in bits (big-endian).
    Digest finish() {
        std::uint64_t bits = total * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        std::uint8_t zero = 0;
        while (buffered != 56) update(&zero, 1);
        std::uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[i] = std::uint8_t(bits >> (56 - 8 * i));
        update(length, 8);

        Digest out;
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = std::uint8_t(state[i] >> 24);
            out[i * 4 + 1] = std::uint8_t(state[i] >> 16);
            out[i * 4 + 2] = std::uint8_t(state[i] >> 8);
            out[i * 4 + 3] = std::uint8_t(state[i]);
        }
        return out;
    }
};

// HMAC with the key blocks already hashed once, reused for every message.
struct Hmac {
    Sha256 inner, outer;

    explicit Hmac(const Bytes& key) {
        Bytes k = key;
        if (k.size() > 64) {
            Sha256 h;
            h.update(k.data(), k.size());
            Digest d = h.finish();
            k.assign(d.begin(), d.end());
        }
        k.resize(64, 0);
        std::uint8_t ipad[64], opad[64];
        for (int i = 0; i < 64; ++i) {
            ipad[i] = k[i] ^ 0x36;
            opad[i] = k[i] ^ 0x5c;
        }
        inner.update(ipad, 64);
        outer.update(opad, 64);
    }

    Digest mac(const std::uint8_t* data, std::size_t size) const {
        Sha256 in = inner;
        in.update(data, size);
        Digest innerHash = in.finish();
        Sha256 out = outer;
        out.update(innerHash.data(), innerHash.size());
        return out.finish();
    }
};

}  // namespace

Digest sha256(const Bytes& data) {
    Sha256 h;
    h.update(data.data(), data.size());
    return h.finish();
}

Digest hmacSha256(const Bytes& key, const Bytes& message) {
    return Hmac(key).mac(message.data(), message.size());
}

Bytes pbkdf2Sha256(const std::string& password, const Bytes& salt,
                   std::uint32_t iterations, std::size_t length) {
    if (iterations == 0) throw std::invalid_argument("iterations must be at least 1");
    const Hmac prf(Bytes(password.begin(), password.end()));
    Bytes out;
    out.reserve(length);

    for (std::uint32_t block = 1; out.size() < length; ++block) {
        Bytes first = salt;
        first.push_back(std::uint8_t(block >> 24));
        first.push_back(std::uint8_t(block >> 16));
        first.push_back(std::uint8_t(block >> 8));
        first.push_back(std::uint8_t(block));

        Digest u = prf.mac(first.data(), first.size());
        Digest t = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = prf.mac(u.data(), u.size());
            for (int j = 0; j < 32; ++j) t[j] ^= u[j];
        }
        for (int j = 0; j < 32 && out.size() < length; ++j) out.push_back(t[j]);
    }
    return out;
}

Bytes randomBytes(std::size_t count) {
    Bytes out(count);
    if (count == 0) return out;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(count),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("could not read secure random bytes");
    }
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(count))) {
        throw std::runtime_error("could not read secure random bytes");
    }
#endif
    return out;
}

std::string toHex(const Bytes& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

Bytes fromHex(const std::string& hex) {
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (hex.size() % 2) return {};
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = value(hex[i]), lo = value(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(std::uint8_t(hi * 16 + lo));
    }
    return out;
}

bool constantTimeEqual(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return false;
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

}  // namespace draz::crypto
