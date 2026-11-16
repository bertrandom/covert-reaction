// MicroProse .PIC pictures (covert-decomp src/ASM/GFX.ASM `pic_begin_decode`,
// `lzw_start`, `lzw_decode`, `lzw_next_byte`; loaded by FILEIO.C load_picture,
// load_image_offscreen, decode_picture_from_memory). The same encoding holds
// the images inside .PAN animations.
//
// Layout (little-endian words; the decoder reads the stream a word at a time):
//   +0  flags   bit 0: 4-bit pixels, two per byte, low nibble first ("nibbles")
//               bit 3: a 16-byte colour map follows the header
//               bit 4: a 128-byte colour map follows (if bit 3 is clear)
//               (all of Covert Action's files have flags 07h or 0Fh)
//   +2  width, +4 height
//   [+6 colour map: CGA/EGA dithering bytes handed to drv_set_color_map; the
//       MCGA driver ignores it, see McgaPalette.h]
//   then a word whose low byte is the maximum LZW code width (capped at 11) and
//   whose high byte is the first byte of the code stream.
// The pixel data is LZW (codes LSB-first, 9 bits growing to the maximum; when
// the table would need one more bit it's reset to the 256 single bytes, there
// is no clear code), and the LZW output is run-length coded: 90h n (n > 0)
// repeats the previous byte n-1 more times, 90h 00 is a literal 90h. With
// nibbles, every output byte is two pixels. Rows follow each other in one
// stream; there are no palettes in the file: colours are 0..15 (see McgaPalette.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/Image.h"

namespace reaction {

// The LZW + run-length decoder state (lzw_start .. lzw_decode). It reads its
// input as words, like the original, so position() is where a following item of
// a .PAN file starts.
class PicDecoder {
public:
    // Starts at `pos` (lzw_start): reads the max-code-width word.
    PicDecoder(std::span<const std::uint8_t> data, std::size_t pos);

    // lzw_decode: `count` pixels into `out` (with nibbles, rounded up to an even
    // number of pixels, so `out` needs room for count + 1).
    void decode(std::span<std::uint8_t> out, int count, bool nibbles);

    std::size_t position() const { return pos_; }

private:
    std::uint16_t nextWord();
    std::uint8_t nextByte();  // lzw_next_byte
    void resetTable();        // lzw_init

    static constexpr int kTableSize = 0x800;  // 11-bit codes at most

    std::span<const std::uint8_t> data_;
    std::size_t pos_;
    // Code reader: the last word read and how many of its high bits are unused.
    std::uint16_t input_ = 0;
    int inputBits_ = 8;
    int bits_ = 9, maxBits_ = 11;
    int mask_ = 0x1FF, nextCode_ = 0x100;
    int prevCode_ = 0;
    std::uint8_t firstChar_ = 0;
    std::vector<std::uint16_t> prefix_ = std::vector<std::uint16_t>(kTableSize, 0xFFFF);
    std::vector<std::uint8_t> suffix_ = std::vector<std::uint8_t>(kTableSize, 0);
    std::vector<std::uint8_t> pending_;  // the current string, last byte first (the original's stack)
    // Run-length layer.
    std::uint8_t run_ = 0;
    std::uint8_t runByte_ = 0;
};

struct PicHeader {
    std::uint16_t flags = 0;
    int width = 0, height = 0;
    bool nibbles() const { return flags & 1; }
    std::vector<std::uint8_t> colorMap;  // 0, 16 or 128 bytes (unused on MCGA)
};

struct Picture {
    PicHeader header;
    Image image;  // width x height, colour indices (0..15 for 4-bit pictures)
};

// Decodes a picture starting at `pos` (pic_begin_decode + one lzw_decode per
// row). If `end` is given, it receives the offset just past the words the
// decoder consumed. Throws std::runtime_error on a malformed header.
Picture decodePicture(std::span<const std::uint8_t> data, std::size_t pos = 0,
                      std::size_t* end = nullptr);

}  // namespace reaction
