#include "core/Png.h"

#include "core/Error.h"
#include "core/File.h"

#include <stdio.h>
#include <string.h>

#include <vector>

namespace cadgeom::core {
namespace {

uint32_t Crc32(const uint8_t* data, size_t length, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

uint32_t Adler32(const uint8_t* data, size_t length) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < length; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void PushBigEndian32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void PushChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    PushBigEndian32(out, static_cast<uint32_t>(data.size()));
    const size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = Crc32(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu;
    PushBigEndian32(out, crc);
}

/// A zlib stream made entirely of stored (uncompressed) deflate blocks. Every
/// decoder handles them, and it removes any need for a compression library.
std::vector<uint8_t> ZlibStore(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    out.push_back(0x78);  // CM = deflate, CINFO = 32K window
    out.push_back(0x01);  // FCHECK, no preset dictionary, fastest compression

    size_t offset = 0;
    do {
        const size_t blockSize = (raw.size() - offset) < 65535u ? (raw.size() - offset) : 65535u;
        const bool last = (offset + blockSize) >= raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<uint8_t>(blockSize & 0xFFu));
        out.push_back(static_cast<uint8_t>(blockSize >> 8));
        out.push_back(static_cast<uint8_t>(~blockSize & 0xFFu));
        out.push_back(static_cast<uint8_t>((~blockSize >> 8) & 0xFFu));
        out.insert(out.end(), raw.begin() + static_cast<ptrdiff_t>(offset),
                   raw.begin() + static_cast<ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    } while (offset < raw.size());

    PushBigEndian32(out, Adler32(raw.data(), raw.size()));
    return out;
}

} // namespace

CgResult WritePng(const char* utf8Path, uint32_t width, uint32_t height, const uint8_t* rgba,
                  size_t rowPitchBytes) {
    if (!utf8Path || !*utf8Path) {
        return SetError(CgResult::InvalidArgument, "WritePng: empty path");
    }
    if (!rgba || width == 0 || height == 0) {
        return SetError(CgResult::InvalidArgument, "WritePng: nothing to write");
    }

    // Each scanline is prefixed with its filter type. Filter 0 (none) keeps the
    // encoder trivial; the size cost is irrelevant for a screenshot.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgba + static_cast<size_t>(y) * rowPitchBytes;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    std::vector<uint8_t> header;
    PushBigEndian32(header, width);
    PushBigEndian32(header, height);
    header.push_back(8);  // bit depth
    header.push_back(6);  // colour type: RGBA
    header.push_back(0);  // compression: deflate
    header.push_back(0);  // filter method
    header.push_back(0);  // no interlacing

    std::vector<uint8_t> file{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    PushChunk(file, "IHDR", header);
    PushChunk(file, "IDAT", ZlibStore(raw));
    PushChunk(file, "IEND", {});

    return WriteFile(utf8Path, file.data(), file.size());
}

} // namespace cadgeom::core
