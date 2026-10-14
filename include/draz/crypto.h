#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace draz::crypto {

using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;

// SHA-256 (FIPS 180-4).
Digest sha256(const Bytes& data);

// HMAC-SHA256 (RFC 2104).
Digest hmacSha256(const Bytes& key, const Bytes& message);

// PBKDF2-HMAC-SHA256 (RFC 8018). Slow on purpose, so guessing passwords
// from a stolen data file is expensive.
Bytes pbkdf2Sha256(const std::string& password, const Bytes& salt,
                   std::uint32_t iterations, std::size_t length);

// Random bytes from the operating system's secure random source.
Bytes randomBytes(std::size_t count);

std::string toHex(const Bytes& bytes);
Bytes fromHex(const std::string& hex);  // empty on invalid input

// Compares without stopping at the first difference, so the time taken
// does not reveal how many leading bytes matched.
bool constantTimeEqual(const Bytes& a, const Bytes& b);

}  // namespace draz::crypto
