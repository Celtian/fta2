/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "saga2/shorten.h"
#include "platform/runtime.h"
#include <bit>

// Original decoder based on etree's Shorten 3.6.1 and Java-Shorten-decoder:
// http://shnutils.freeshell.org/shorten/
// https://github.com/soiaf/Java-Shorten-decoder
// Based on the imported SAGA2 Shorten decoder. Preserve the original raw PCM
// format and 22050 Hz rate; bound reads and keep predictor history across blocks.
namespace Saga2 {
namespace {
constexpr size_t kMaxDecodedBytes = 32 * 1024 * 1024;
constexpr unsigned kMaxBlock = 65536;
constexpr unsigned kMaxHistory = 64;

class RiceReader {
    Native::ReadStream& _stream;
    byte _byte = 0;
    unsigned _bits = 0;

    unsigned bit() {
        if (!_bits) {
            if (_stream.read(&_byte, 1) != 1)
                throw std::runtime_error("Truncated Shorten bitstream");
            _bits = 8;
        }
        return (_byte >> --_bits) & 1;
    }

  public:
    explicit RiceReader(Native::ReadStream& stream) : _stream(stream) {}
    uint32 rice(unsigned bits) {
        if (bits > 31)
            throw std::runtime_error("Invalid Shorten Rice width");
        uint64 value = 0;
        while (!bit()) {
            if (++value > (UINT32_MAX >> bits))
                throw std::runtime_error("Shorten Rice value overflow");
        }
        for (unsigned i = 0; i < bits; ++i)
            value = value * 2 + bit();
        return uint32(value);
    }
    int32 signedRice(int bits) {
        if (bits < -1 || bits > 30)
            throw std::runtime_error("Invalid Shorten residual width");
        const uint32 value = rice(unsigned(bits + 1));
        return value & 1 ? ~int32(value >> 1) : int32(value >> 1);
    }
    uint32 integer(unsigned bits, unsigned version) { return rice(version == 0 ? bits : rice(2)); }
};

int32 checkedSample(int64 value) {
    if (value < INT32_MIN || value > INT32_MAX)
        throw std::runtime_error("Shorten predictor overflow");
    return int32(value);
}

std::vector<byte> decode(Native::ReadStream& stream, byte& flags) {
    byte header[5];
    if (stream.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "ajkg", 4) != 0 ||
        header[4] > 3)
        throw std::runtime_error("Unsupported Shorten header");
    const unsigned version = header[4];
    RiceReader reader(stream);
    const unsigned type = reader.integer(4, version);
    if (type < 1 || type > 6)
        throw std::runtime_error("Unsupported Shorten PCM type");
    const bool wide = type >= 3;
    const bool unsignedPcm = type == 2 || type == 4 || type == 6;
    flags = (wide ? Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN : 0) |
            (unsignedPcm ? Audio::FLAG_UNSIGNED : 0);
    const unsigned channels = reader.integer(0, version);
    if (channels != 1 && channels != 2)
        throw std::runtime_error("Unsupported Shorten channel count");
    if (channels == 2)
        flags |= Audio::FLAG_STEREO;
    unsigned blockSize = 256, maxLpc = 0, meanCount = 0;
    std::vector<byte> output;
    auto append = [&](byte value) {
        if (output.size() == kMaxDecodedBytes)
            throw std::runtime_error("Shorten output exceeds voice limit");
        output.push_back(value);
    };
    if (version) {
        blockSize = reader.integer(8, version);
        maxLpc = reader.integer(2, version);
        meanCount = reader.integer(0, version);
        const unsigned skip = reader.integer(1, version);
        if (!blockSize || blockSize > kMaxBlock || maxLpc > kMaxHistory ||
            meanCount > kMaxHistory || skip > 1024 * 1024)
            throw std::runtime_error("Invalid Shorten buffer dimensions");
        for (unsigned i = 0; i < skip; ++i)
            append(byte(reader.integer(7, version)));
    }
    const unsigned wrap = std::max(3u, maxLpc);
    const int32 initialOffset = unsignedPcm ? (wide ? 32768 : 128) : 0;
    std::array<std::vector<int32>, 2> buffer, offsets;
    for (unsigned channel = 0; channel < channels; ++channel) {
        buffer[channel].resize(wrap + blockSize);
        offsets[channel].assign(std::max(1u, meanCount), initialOffset);
    }
    unsigned channel = 0, shift = 0;
    for (;;) {
        const unsigned command = reader.rice(2);
        if (command == 4) { // Quit.
            if (channel || output.empty())
                throw std::runtime_error("Incomplete Shorten audio block");
            return output;
        }
        if (command == 5) { // Block size.
            const unsigned next = reader.integer(std::bit_width(blockSize) - 1, version);
            if (channel || !next || next > kMaxBlock)
                throw std::runtime_error("Invalid Shorten block size");
            blockSize = next;
            for (unsigned c = 0; c < channels; ++c)
                buffer[c].resize(wrap + blockSize);
            continue;
        }
        if (command == 6) { // Bit shift.
            shift = reader.rice(2);
            if (shift > 32)
                throw std::runtime_error("Invalid Shorten sample shift");
            continue;
        }
        if (command == 9) { // Verbatim bytes (raw SAGA2 streams have no WAV header).
            unsigned count = reader.rice(5);
            if (count > kMaxDecodedBytes - output.size())
                throw std::runtime_error("Invalid Shorten verbatim length");
            while (count--)
                append(byte(reader.rice(8)));
            continue;
        }
        if (command > 8)
            throw std::runtime_error("Unknown Shorten command");
        const uint32 residual = command == 8 ? 0 : reader.rice(3);
        if (residual > 31)
            throw std::runtime_error("Invalid Shorten residual size");
        const int energy = int(residual) - (version == 0 && command != 8 ? 1 : 0);
        if (energy < -1 || energy > 30)
            throw std::runtime_error("Invalid Shorten residual size");
        auto& data = buffer[channel];
        auto& offset = offsets[channel];
        int64 center = offset[0];
        if (meanCount) {
            center = version < 2 ? 0 : meanCount / 2;
            for (auto value : offset)
                center += value;
            center /= meanCount;
            if (version >= 2)
                center >>= shift;
        }
        unsigned order = command <= 3 ? command : 0;
        std::array<int32, kMaxHistory> coefficients{};
        if (command == 7) {
            order = reader.rice(2);
            if (order > maxLpc)
                throw std::runtime_error("Shorten LPC order exceeds history");
            for (unsigned i = 0; i < order; ++i) {
                coefficients[i] = reader.signedRice(5);
                if (coefficients[i] < -65535 || coefficients[i] > 65535)
                    throw std::runtime_error("Invalid Shorten LPC coefficient");
            }
            for (unsigned i = wrap - order; i < wrap; ++i)
                data[i] = checkedSample(int64(data[i]) - center);
        } else if (order) {
            coefficients[0] = order;
            if (order >= 2)
                coefficients[1] = order == 2 ? -1 : -3;
            if (order == 3)
                coefficients[2] = 1;
        }
        for (unsigned i = 0; i < blockSize; ++i) {
            int64 value = 0;
            if (command != 8) {
                int64 prediction = command == 7 ? (version >= 2 ? 32 : 0) : (order ? 0 : center);
                for (unsigned j = 0; j < order; ++j)
                    prediction += int64(coefficients[j]) * data[wrap + i - j - 1];
                if (command == 7)
                    prediction >>= 5;
                value = int64(reader.signedRice(energy)) + prediction;
            }
            data[wrap + i] = checkedSample(value);
        }
        if (command == 7)
            for (unsigned i = 0; i < blockSize; ++i)
                data[wrap + i] = checkedSample(int64(data[wrap + i]) + center);
        if (meanCount) {
            int64 sum = version < 2 ? 0 : blockSize / 2;
            for (unsigned i = 0; i < blockSize; ++i)
                sum += data[wrap + i];
            std::move(offset.begin() + 1, offset.end(), offset.begin());
            offset.back() =
                checkedSample((sum / blockSize) * (version >= 2 ? int64(1) << shift : 1));
        }
        // Retain unshifted predictor history, including across short final blocks.
        std::copy(data.begin() + blockSize, data.begin() + blockSize + wrap, data.begin());
        for (unsigned i = 0; i < blockSize; ++i)
            data[wrap + i] = shift == 32 ? 0 : checkedSample(int64(data[wrap + i]) * (int64(1) << shift));
        if (++channel == channels) {
            const int low = unsignedPcm ? 0 : (wide ? -32768 : -128);
            const int high = unsignedPcm ? (wide ? 65535 : 255) : (wide ? 32767 : 127);
            for (unsigned i = 0; i < blockSize; ++i) {
                for (unsigned c = 0; c < channels; ++c) {
                    const uint16 value = uint16(std::clamp(buffer[c][wrap + i], low, high));
                    append(byte(value));
                    if (wide)
                        append(byte(value >> 8));
                }
            }
            channel = 0;
        }
    }
}
} // namespace

byte* loadShortenFromStream(Native::ReadStream& stream, int& size, int& rate, byte& flags) {
    size = 0;
    rate = 22050;
    flags = 0;
    try {
        auto pcm = decode(stream, flags);
        auto* result = static_cast<byte*>(std::malloc(pcm.size()));
        if (!result)
            throw std::bad_alloc();
        memcpy(result, pcm.data(), pcm.size());
        size = int(pcm.size());
        return result;
    } catch (const std::exception& e) {
        flags = 0;
        warning("Cannot decode voice: %s", e.what());
        return nullptr;
    }
}

Audio::AudioStream* makeShortenStream(Native::SeekableReadStream& stream) {
    int size, rate;
    byte flags;
    byte* data = loadShortenFromStream(stream, size, rate, flags);
    return data ? Audio::makeRawStream(data, size, rate, flags) : nullptr;
}
} // namespace Saga2
