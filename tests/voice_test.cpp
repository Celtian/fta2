// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/resources.h"
#include "saga2/hresmgr.h"
#include "saga2/shorten.h"
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Writer {
    std::vector<byte> bytes{'a', 'j', 'k', 'g', 1};
    unsigned bits = 0;
    void bit(unsigned value) {
        if (!bits)
            bytes.push_back(0);
        bytes.back() |= (value & 1) << (7 - bits);
        bits = (bits + 1) % 8;
    }
    void rice(uint32 value, unsigned width) {
        for (uint32 i = 0; i < (value >> width); ++i)
            bit(0);
        bit(1);
        for (unsigned i = width; i > 0; --i)
            bit(value >> (i - 1));
    }
    void integer(uint32 value) {
        rice(12, 2);
        rice(value, 12);
    }
    void signedRice(int value, unsigned width = 8) {
        rice(value < 0 ? uint32(-int64(value) * 2 - 1) : uint32(value) * 2, width + 1);
    }
    Writer(unsigned type = 5, unsigned channels = 1, unsigned mean = 0) {
        for (auto value : {type, channels, 4u, 3u, mean, 0u})
            integer(value);
    }
    void block(unsigned command, std::initializer_list<int> residuals) {
        rice(command, 2);
        rice(8, 3);
        for (int value : residuals)
            signedRice(value);
    }
    void quit() { rice(4, 2); }
};
std::unique_ptr<Audio::AudioStream> decode(const std::vector<byte>& bytes) {
    Native::MemoryReadStream stream(bytes.data(), bytes.size());
    return std::unique_ptr<Audio::AudioStream>(Saga2::makeShortenStream(stream));
}
void synthetic() {
    Writer w;
    w.block(0, {1, 2, 3, 4});
    w.block(1, {1, 1, 1, 1});
    w.block(2, {0, 0, 0, 0});
    w.block(3, {0, 0, 0, 0});
    w.rice(7, 2);
    w.rice(8, 3);
    w.rice(1, 2);
    w.signedRice(32, 5);
    for (int value : {1, 1, 1, 1})
        w.signedRice(value);
    w.rice(5, 2);
    w.integer(2);
    w.rice(6, 2);
    w.rice(1, 2);
    w.block(1, {1, 1});
    w.rice(8, 2);
    w.quit();
    auto result = decode(w.bytes);
    check(result && result->rate == 22050 && result->samples.size() == 24,
          "Shorten command sequence failed");
    for (int i = 0; i < 20; ++i)
        check(result->samples[i] == float(i + 1) / 32768, "Shorten predictor history mismatch");
    check(result->samples[20] == 42.0f / 32768 && result->samples[21] == 44.0f / 32768 &&
              result->samples[22] == 0 && result->samples[23] == 0,
          "Shorten final block/shift mismatch");
    for (size_t length = 0; length + 1 < w.bytes.size(); ++length) {
        auto truncated = std::vector<byte>(w.bytes.begin(), w.bytes.begin() + length);
        check(!decode(truncated), "Truncated Shorten stream was accepted");
    }
    Writer stereo(5, 2);
    stereo.block(0, {100, 200, 300, 400});
    stereo.block(0, {-100, -200, -300, -400});
    stereo.quit();
    result = decode(stereo.bytes);
    check(result && result->channels == 2 && result->samples.size() == 8 &&
              result->samples[0] == -result->samples[1],
          "Shorten stereo channels were lost");
    Writer u8(2);
    u8.block(0, {-128, -64, 0, 127});
    u8.quit();
    result = decode(u8.bytes);
    check(result && result->samples == std::vector<float>({-1, -0.5f, 0, 127.0f / 128}),
          "Unsigned Shorten PCM offset mismatch");
    Writer oversized;
    oversized.rice(5, 2);
    oversized.integer(65537);
    oversized.quit();
    check(!decode(oversized.bytes), "Oversized Shorten block accepted");
    Writer invalidShift;
    invalidShift.rice(6, 2);
    invalidShift.rice(33, 2);
    invalidShift.quit();
    check(!decode(invalidShift.bytes), "Invalid Shorten shift accepted");
    Saga2::hResEntry entry;
    entry.offset = 0x10012345;
    check(entry.resOffset() == 0x10012345, "Large voice archive offset was truncated");
}
void audit(const Native::Path& archive, const Native::Path& output) {
    Native::File file;
    check(file.open(archive), "Cannot open voice archive for audit");
    std::vector<byte> bytes(file.size());
    check(file.read(bytes.data(), bytes.size()) == bytes.size(), "Cannot read voice archive");
    file.close();
    Native::validateHrs(bytes.data(), bytes.size());
    std::ofstream report(output.c_str());
    check(bool(report), "Cannot write voice audit");
    auto root = READ_LE_UINT32(bytes.data() + 4);
    auto end = root + READ_LE_UINT32(bytes.data() + 8);
    size_t count = 0;
    for (uint32 pos = root; pos < end; pos += 12) {
        if (memcmp(bytes.data() + pos, "TALK", 4))
            continue;
        auto table = READ_LE_UINT32(bytes.data() + pos + 4);
        auto size = READ_LE_UINT32(bytes.data() + pos + 8);
        for (uint32 item = table; item < table + size; item += 12) {
            auto offset = READ_LE_UINT32(bytes.data() + item + 4);
            auto length = READ_LE_UINT32(bytes.data() + item + 8);
            check(!(offset & 0x80000000), "External voice resource");
            Native::MemoryReadStream stream(bytes.data() + offset, length);
            int pcmSize, rate;
            byte flags;
            std::unique_ptr<byte, decltype(&std::free)> pcm(
                Saga2::loadShortenFromStream(stream, pcmSize, rate, flags), &std::free);
            check(bool(pcm) && pcmSize > 0, "Voice archive contains undecodable recording");
            report.write(reinterpret_cast<char*>(bytes.data() + item), 4);
            report << '\t' << offset << '\t' << length << '\t' << pcmSize << '\t' << rate << '\t'
                   << unsigned(flags) << '\t' << Native::sha256(pcm.get(), pcmSize) << '\n';
            ++count;
        }
    }
    check(count > 0, "Voice archive has no recordings");
    std::cout << "Decoded recordings: " << count << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        synthetic();
        if (argc == 3)
            audit(std::filesystem::absolute(argv[1]), argv[2]);
        else
            check(argc == 1, "Usage: fta2-voice-test [ARCHIVE AUDIT.tsv]");
        std::cout << "Voice checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
