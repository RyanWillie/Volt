#include <volt/core/content_hash.hpp>

#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {

namespace {

constexpr auto kSha256Prefix = std::string_view{"sha256:"};
constexpr auto kSha256HexLength = std::size_t{64};

[[nodiscard]] bool is_lower_hex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32U - bits));
}

[[nodiscard]] std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

[[nodiscard]] std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] std::uint32_t big_sigma0(std::uint32_t x) noexcept {
    return rotate_right(x, 2U) ^ rotate_right(x, 13U) ^ rotate_right(x, 22U);
}

[[nodiscard]] std::uint32_t big_sigma1(std::uint32_t x) noexcept {
    return rotate_right(x, 6U) ^ rotate_right(x, 11U) ^ rotate_right(x, 25U);
}

[[nodiscard]] std::uint32_t small_sigma0(std::uint32_t x) noexcept {
    return rotate_right(x, 7U) ^ rotate_right(x, 18U) ^ (x >> 3U);
}

[[nodiscard]] std::uint32_t small_sigma1(std::uint32_t x) noexcept {
    return rotate_right(x, 17U) ^ rotate_right(x, 19U) ^ (x >> 10U);
}

[[nodiscard]] std::vector<std::uint8_t> padded_sha256_message(std::string_view bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::invalid_argument{"Content is too large to hash with SHA-256"};
    }

    auto message = std::vector<std::uint8_t>{};
    message.reserve(bytes.size() + 72U);
    for (const auto byte : bytes) {
        message.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    }

    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8ULL;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U) {
        message.push_back(0U);
    }
    for (auto shift = 56U; shift <= 56U; shift -= 8U) {
        message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
        if (shift == 0U) {
            break;
        }
    }
    return message;
}

[[nodiscard]] std::array<std::uint32_t, 8> sha256_words(std::string_view bytes) {
    static constexpr auto k = std::array<std::uint32_t, 64>{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};

    auto hash = std::array<std::uint32_t, 8>{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    const auto message = padded_sha256_message(bytes);

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
        auto words = std::array<std::uint32_t, 64>{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto offset = chunk + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[offset]) << 24U) |
                           (static_cast<std::uint32_t>(message[offset + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(message[offset + 2U]) << 8U) |
                           static_cast<std::uint32_t>(message[offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            words[index] = small_sigma1(words[index - 2U]) + words[index - 7U] +
                           small_sigma0(words[index - 15U]) + words[index - 16U];
        }

        auto a = hash[0];
        auto b = hash[1];
        auto c = hash[2];
        auto d = hash[3];
        auto e = hash[4];
        auto f = hash[5];
        auto g = hash[6];
        auto h = hash[7];

        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto t1 = h + big_sigma1(e) + choose(e, f, g) + k[index] + words[index];
            const auto t2 = big_sigma0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    return hash;
}

} // namespace

ContentHash::ContentHash(std::string value) : value_{std::move(value)} {
    if (value_.size() != kSha256Prefix.size() + kSha256HexLength ||
        value_.compare(0U, kSha256Prefix.size(), kSha256Prefix) != 0) {
        throw std::invalid_argument{"Content hash must use sha256:<64 lowercase hex digits>"};
    }
    for (auto index = kSha256Prefix.size(); index < value_.size(); ++index) {
        if (!is_lower_hex(value_[index])) {
            throw std::invalid_argument{"Content hash must use lowercase hexadecimal digits"};
        }
    }
}

[[nodiscard]] ContentHash sha256_content_hash(std::string_view bytes) {
    auto stream = std::ostringstream{};
    stream << "sha256:" << std::hex << std::setfill('0');
    for (const auto word : sha256_words(bytes)) {
        stream << std::setw(8) << word;
    }
    return ContentHash{stream.str()};
}

} // namespace volt
