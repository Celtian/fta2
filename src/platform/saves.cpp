// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/saves.h"
#include <set>
namespace Native {
uint32 validateSave(SeekableReadStream& in) {
    if (in.size() < 128 || !in.seek(0) || in.readUint32BE() != MKTAG('F', 'T', 'A', '2'))
        throw std::runtime_error("Not an FTA2 save file");
    in.seek(128);
    std::set<uint32> found;
    uint32 seed = 0;
    while (in.pos() < in.size()) {
        if (in.size() - in.pos() < 8)
            throw std::runtime_error("Truncated save chunk header");
        uint32 tag = in.readUint32BE(), n = in.readUint32LE();
        if (n > uint64(in.size() - in.pos()))
            throw std::runtime_error("Save chunk extends outside file");
        if (!found.insert(tag).second)
            throw std::runtime_error("Duplicate save chunk");
        if (tag == MKTAG('N', 'R', 'N', 'G')) {
            if (n != 4)
                throw std::runtime_error("Invalid native RNG save chunk");
            seed = in.readUint32LE();
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
