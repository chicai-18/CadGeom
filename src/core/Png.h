// Minimal PNG writer, for IViewport::SaveScreenshot.
//
// PNG rather than a raw dump because a screenshot is something a person opens,
// and self-written rather than vendored because the whole encoder is a hundred
// lines: an uncompressed deflate stream is a legal zlib stream, so the file is
// valid everywhere at the cost of being a little larger than it needs to be.
// Image compression is not a job the engine should be spending a dependency on.
#pragma once

#include <cadgeom/Types.h>

#include <stdint.h>

namespace cadgeom::core {

/// Writes 8-bit RGBA, top row first, `width * height * 4` bytes.
/// `utf8Path` is created or truncated.
CgResult WritePng(const char* utf8Path, uint32_t width, uint32_t height, const uint8_t* rgba,
                  size_t rowPitchBytes);

} // namespace cadgeom::core
