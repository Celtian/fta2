// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/saves.h"
#include "platform/localization.h"
#include "platform/resources.h"
#include <set>
namespace Native {
uint32 validateSave(SeekableReadStream& in) {
    if (in.size() < 128 || !in.seek(0) || in.readUint32BE() != MKTAG('F', 'T', 'A', '2'))
        throw std::runtime_error("Not an FTA2 save file");
    in.seek(128);
    std::set<uint32> found;
    std::vector<byte> speechData;
    uint32 seed = 0;
    while (in.pos() < in.size()) {
        if (in.size() - in.pos() < 8)
            throw std::runtime_error("Truncated save chunk header");
        uint32 tag = in.readUint32BE(), n = in.readUint32LE();
        if (n > uint64(in.size() - in.pos()))
            throw std::runtime_error("Save chunk extends outside file");
        if (!found.insert(tag).second)
            throw std::runtime_error("Duplicate save chunk");
        if (tag == MKTAG('S', 'P', 'C', 'H')) {
            speechData.resize(n);
            in.read(speechData.data(), n);
        } else if (tag == MKTAG('N', 'R', 'N', 'G')) {
            if (n != 4)
                throw std::runtime_error("Invalid native RNG save chunk");
            seed = in.readUint32LE();
        } else if (tag == MKTAG('N', 'L', 'O', 'C')) {
            const auto end = in.pos() + n;
            if (!found.contains(MKTAG('S', 'P', 'C', 'H')) || n < 4 || in.readUint16LE() != 1)
                throw std::runtime_error("Invalid native localization save header");
            const auto count = in.readUint16LE();
            if (count > 20)
                throw std::runtime_error("Too many localized saved speeches");
            std::set<uint16> indices;
            std::vector<String> originalSpeech;
            if (speechData.size() < 2)
                throw std::runtime_error("Native localization requires saved speech records");
            const int originalCount = READ_LE_UINT16(speechData.data());
            if (originalCount > 20)
                throw std::runtime_error("Invalid original speech count");
            size_t cursor = 2;
            for (int i = 0; i < originalCount; ++i) {
                if (speechData.size() - cursor < 22)
                    throw std::runtime_error("Truncated original speech record");
                const auto samples = READ_LE_UINT16(speechData.data() + cursor);
                const auto characters = READ_LE_UINT16(speechData.data() + cursor + 2);
                if (samples >= 50 || characters >= 512 ||
                    22 + size_t(samples) * 4 + characters > speechData.size() - cursor)
                    throw std::runtime_error("Invalid original speech record");
                cursor += 22 + size_t(samples) * 4;
                originalSpeech.emplace_back(
                    reinterpret_cast<const char*>(speechData.data() + cursor), characters);
                cursor += characters;
            }
            if (cursor != speechData.size())
                throw std::runtime_error("Original speech chunk has trailing bytes");
            for (int i = 0; i < count; ++i) {
                if (end - in.pos() < 70)
                    throw std::runtime_error("Truncated native localization record");
                const auto index = in.readUint16LE();
                const auto language = in.readByte();
                if (index >= 20 || !indices.insert(index).second || !language || !validLanguage(language) ||
                    in.readByte() != 0)
                    throw std::runtime_error("Invalid native localization record");
                char hash[65] = {};
                in.read(hash, 64);
                for (int j = 0; j < 64; ++j) {
                    const char c = hash[j];
                    if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f'))
                        throw std::runtime_error("Invalid native localization fingerprint");
                }
                if (index >= originalSpeech.size() ||
                    sha256(reinterpret_cast<const byte*>(originalSpeech[index].data()),
                           originalSpeech[index].size()) != hash)
                    throw std::runtime_error("Native localization does not match original speech");
                const auto length = in.readUint16LE();
                if (length > 8192 || length > end - in.pos())
                    throw std::runtime_error("Invalid native localization text size");
                int choices = 0;
                for (int j = 0; j < length; ++j) {
                    const auto c = in.readByte();
                    if (!c)
                        throw std::runtime_error("NUL in native localization text");
                    if (c == '@')
                        ++choices;
                }
                if (choices !=
                    std::count(originalSpeech[index].begin(), originalSpeech[index].end(), '@'))
                    throw std::runtime_error("Native localization changed dialogue choices");
            }
            if (in.pos() != end)
                throw std::runtime_error("Native localization chunk has trailing bytes");
        } else
            in.skip(n);
    }
    constexpr const char* required[] = {
        "GLOB", "TIME", "CALE", "WRLD", "ACTR", "OBJS", "BAND", "PLYR", "CNTR", "TAGS",
        "CYCL", "SDTA", "SAGA", "MOTN", "TSTK", "TASK", "TACT", "SPCH", "AREG", "TIMR",
        "SENS", "ACNT", "MISS", "FACT", "TMST", "SPEL", "AMAP", "UIST", "PALE", "CONT"};
    for (auto name : required)
        if (!found.contains(READ_BE_UINT32(name)))
            throw std::runtime_error(std::string("Required save chunk is missing: ") + name);
    in.seek(0);
    return seed;
}
} // namespace Native
