// What the tests over the real-font corpus share: the list of faces the
// digests in tests/golden/font-wild name (tools/oracle/FORMAT-font-wild.md),
// and the check that a file on this machine is the one that was dumped.
//
// The fonts are not in the repository. A face is read from the absolute path
// its digest records, and only if the file there has the recorded length and
// SHA-256; a test skips, and counts as skipped, any other.

#pragma once

#include "golden.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace wild
{

// --- SHA-256 (FIPS 180-4), for telling that a file is the one that was dumped --

struct Sha256 {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t{p[4 * i]} << 24) | (uint32_t{p[4 * i + 1]} << 16) |
                   (uint32_t{p[4 * i + 2]} << 8) | uint32_t{p[4 * i + 3]};
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
};

inline std::string sha256_hex(const uint8_t* data, size_t size)
{
    Sha256 s;
    size_t at = 0;
    for (; at + 64 <= size; at += 64)
        s.block(data + at);
    uint8_t tail[128] = {};
    const size_t rest = size - at;
    if (rest != 0)
        std::memcpy(tail, data + at, rest);
    tail[rest] = 0x80;
    const size_t padded = rest + 9 <= 64 ? 64 : 128;
    const uint64_t bits = static_cast<uint64_t>(size) * 8u;
    for (int i = 0; i < 8; ++i)
        tail[padded - 1 - static_cast<size_t>(i)] = static_cast<uint8_t>(bits >> (8 * i));
    s.block(tail);
    if (padded == 128)
        s.block(tail + 64);
    char out[65];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + 8 * i, 9, "%08x", static_cast<unsigned>(s.h[i]));
    return std::string(out, 64);
}

// One face of the corpus, as the digest's FACE record names it.
struct CorpusFace {
    std::string path;
    int index = 0;      // within its file
    int face_count = 0; // of its file
    int64_t byte_length = 0;
    std::string sha256;
    long ordinal = 0; // among all faces, in digest order
    bool has_math = false;

    std::string label() const { return path + "#" + std::to_string(index); }
};

// Every face of every digest file, in (file, record) order.
inline std::vector<CorpusFace> corpus_faces()
{
    std::vector<CorpusFace> out;
    for (const std::string& path : golden::list("font-wild")) {
        if (path.size() >= 13 && path.compare(path.size() - 13, 13, "/index.golden") == 0)
            continue;
        golden::File file;
        if (!file.load(path))
            continue;
        bool open = false;
        for (const golden::Record& r : file) {
            if (r.is("FACE")) {
                CorpusFace face;
                face.path = r.str(0);
                face.index = r.i32(1);
                face.face_count = r.i32(2);
                face.byte_length = r.i64(3);
                face.sha256 = r.str(4);
                face.ordinal = static_cast<long>(out.size());
                out.push_back(face);
                open = true;
            } else if (r.is("END")) {
                open = false;
            } else if (open && r.is("MATHCONSTS")) {
                out.back().has_math = true;
            }
        }
    }
    return out;
}

// The bytes of a corpus file, read once however many faces it holds.
struct CorpusFile {
    std::string path;
    std::vector<uint8_t> bytes;
    std::string sha256; // "absent" if the file cannot be read; empty if its length is wrong

    // 0 the file is the one dumped, 1 it is absent, 2 it has changed.
    int open(const CorpusFace& face)
    {
        if (path != face.path) {
            path = face.path;
            sha256.clear();
            bytes.clear();
            std::FILE* probe = std::fopen(face.path.c_str(), "rb");
            if (probe == nullptr) {
                sha256 = "absent";
            } else {
                std::fclose(probe);
                if (!golden::read_file(face.path, bytes))
                    sha256 = "absent";
            }
            if (sha256.empty() && static_cast<int64_t>(bytes.size()) == face.byte_length)
                sha256 = sha256_hex(bytes.data(), bytes.size());
        }
        if (sha256 == "absent")
            return 1;
        return sha256 == face.sha256 ? 0 : 2;
    }
};

inline bool env_flag(const char* name)
{
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
}

} // namespace wild
