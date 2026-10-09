// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/resources.h"
#include <bit>
namespace Native {
String sha256(const byte* data, size_t size) {
    constexpr uint32 k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32 hash[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t blocks = (size + 9 + 63) / 64;
    for (size_t block = 0; block < blocks; ++block) {
        byte bytes[64] = {};
        size_t offset = block * 64;
        for (size_t i = 0; i < 64; ++i) {
            size_t pos = offset + i;
            if (pos < size)
                bytes[i] = data[pos];
            else if (pos == size)
                bytes[i] = 0x80;
        }
        if (block + 1 == blocks) {
            uint64 bits = uint64(size) * 8;
            for (int i = 0; i < 8; ++i)
                bytes[63 - i] = bits >> (i * 8);
        }
        uint32 w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = READ_BE_UINT32(bytes + i * 4);
        for (int i = 16; i < 64; ++i) {
            uint32 a = w[i - 15], b = w[i - 2];
            w[i] = w[i - 16] + (std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
                   (std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10));
        }
        uint32 a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4], f = hash[5],
               g = hash[6], h = hash[7];
        for (int i = 0; i < 64; ++i) {
            uint32 t1 = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                        ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32 t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
                        ((a & b) ^ (a & c) ^ (b & c));
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
    String result;
    for (auto v : hash)
        result += String::format("%08x", v);
    return result;
}
void requireSeparateOutput(const Path& data, const Path& output) {
    auto d = std::filesystem::weakly_canonical(data), o = std::filesystem::weakly_canonical(output);
    for (auto current = o;; current = current.parent_path()) {
        // equivalent also recognizes existing paths with different case on Windows.
        std::error_code ec;
        if (current == d || std::filesystem::equivalent(d, current, ec))
            throw std::runtime_error(
                "Save/cache directory must be outside the read-only game directory");
        if (current == current.parent_path())
            break;
    }
}
void validateHrs(const byte* data, size_t size) {
    auto range = [size](uint32 offset, uint32 count) {
        if (offset > size || count > size - offset)
            throw std::runtime_error("HRES resource extends outside archive");
    };
    if (size < 12 || memcmp(data, "HRES", 4))
        throw std::runtime_error("Missing HRES archive header");
    uint32 root = READ_LE_UINT32(data + 4), length = READ_LE_UINT32(data + 8);
    range(root, length);
    if (length % 12)
        throw std::runtime_error("Invalid HRES root size");
    for (uint32 p = root; p < root + length; p += 12) {
        uint32 table = READ_LE_UINT32(data + p + 4), n = READ_LE_UINT32(data + p + 8);
        range(table, n);
        if (n % 12)
            throw std::runtime_error("Invalid HRES resource table size");
        for (uint32 t = table; t < table + n; t += 12) {
            uint32 offset = READ_LE_UINT32(data + t + 4), bytes = READ_LE_UINT32(data + t + 8);
            if (!(offset & 0x80000000))
                range(offset, bytes);
        }
    }
}
struct Asset {
    const char* name;
    uint32 offset, size;
};
#include "platform/dos-profile.inc"
void prepareDosResources() {
    requireSeparateOutput(dataDirectory, resourceDirectory);
    File exe;
    if (!exe.open("FTA2.EXE"))
        throw std::runtime_error("Required resource source is missing: FTA2.EXE");
    std::vector<byte> bytes(exe.size());
    exe.read(bytes.data(), bytes.size());
    auto checksum = sha256(bytes.data(), bytes.size());
    if (checksum != dosExeHash)
        throw std::runtime_error("Unsupported FTA2.EXE: expected SHA-256 " +
                                 std::string(dosExeHash) + ", found " + std::string(checksum));
    for (auto& profile : archives) {
        File file;
        if (!file.open(profile.name))
            throw std::runtime_error(std::string("Required game archive is missing: ") +
                                     profile.name);
        std::vector<byte> buffer(file.size());
        file.read(buffer.data(), buffer.size());
        try {
            validateHrs(buffer.data(), buffer.size());
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Invalid game archive ") + profile.name +
                                     ": " + e.what());
        }
        if (sha256(buffer.data(), buffer.size()) != profile.hash)
            throw std::runtime_error(std::string("Unsupported or damaged game archive: ") +
                                     profile.name);
    }
    for (auto& bank : banks) {
        File file;
        if (!file.open(bank.name))
            throw std::runtime_error(std::string("Required instrument bank is missing: ") +
                                     bank.name);
        std::vector<byte> data(file.size());
        file.read(data.data(), data.size());
        if (sha256(data.data(), data.size()) != bank.hash)
            throw std::runtime_error(std::string("Unsupported or damaged instrument bank: ") +
                                     bank.name);
    }
    std::vector<byte> pack(8 + 40 * ARRAYSIZE(assets));
    memcpy(pack.data(), "F2R1", 4);
    WRITE_LE_UINT32(pack.data() + 4, ARRAYSIZE(assets));
    String manifest = "{\n  \"schema_version\": 1,\n  \"profile\": \"english-dos-a7413ddf\",\n  "
                      "\"source\": {\"file\": \"FTA2.EXE\", \"sha256\": \"" +
                      checksum + "\"},\n  \"assets\": {\n";
    for (size_t i = 0; i < ARRAYSIZE(assets); ++i) {
        auto& a = assets[i];
        if (a.offset > bytes.size() || a.size > bytes.size() - a.offset)
            throw std::runtime_error("Invalid compiled DOS profile");
        byte* record = pack.data() + 8 + 40 * i;
        memcpy(record, a.name, strlen(a.name));
        WRITE_LE_UINT32(record + 32, pack.size());
        WRITE_LE_UINT32(record + 36, a.size);
        pack.insert(pack.end(), bytes.begin() + a.offset, bytes.begin() + a.offset + a.size);
        manifest += String::format(
            "    \"%s\": {\"source_offset\": %u, \"size\": %u, \"sha256\": \"%s\"}%s\n", a.name,
            a.offset, a.size, sha256(bytes.data() + a.offset, a.size).c_str(),
            i + 1 == ARRAYSIZE(assets) ? "" : ",");
    }
    auto packHash = sha256(pack.data(), pack.size());
    if (packHash != dosPackHash)
        throw std::runtime_error("DOS resource pack failed its compiled checksum");
    manifest += "  },\n  \"pack_sha256\": \"" + packHash + "\"\n}\n";
    std::filesystem::create_directories(resourceDirectory);
    OutSaveFile out(Path(resourceDirectory / "fta2-dos-resources.bin"));
    out.write(pack.data(), pack.size());
    out.finalize();
    OutSaveFile meta(Path(resourceDirectory / "manifest.json"));
    meta.write(manifest.data(), manifest.size());
    meta.finalize();
}
} // namespace Native
