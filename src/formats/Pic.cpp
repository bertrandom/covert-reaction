#include "formats/Pic.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace reaction {

namespace {

std::uint16_t wordAt(std::span<const std::uint8_t> d, std::size_t pos) {
    // Past the end the original reads whatever its 512-byte buffer holds; zeros here.
    auto at = [&](std::size_t i) -> std::uint16_t { return i < d.size() ? d[i] : 0; };
    return static_cast<std::uint16_t>(at(pos) | at(pos + 1) << 8);
}

}  // namespace

PicDecoder::PicDecoder(std::span<const std::uint8_t> data, std::size_t pos) : data_(data), pos_(pos) {
    // lzw_start: the low byte of the first word is the maximum code width, its
    // high byte already belongs to the code stream (lzw_input_bits = 8).
    const std::uint16_t w = nextWord();
    maxBits_ = std::min(w & 0xFF, 11);
    input_ = w;
    inputBits_ = 8;
    resetTable();
}

std::uint16_t PicDecoder::nextWord() {
    const std::uint16_t w = wordAt(data_, pos_);
    pos_ += 2;
    return w;
}

void PicDecoder::resetTable() {
    // lzw_init: every code a root (prefix FFFFh), 0..255 their own byte. The
    // suffixes of codes >= 256 are left as they are, like the original.
    bits_ = 9;
    mask_ = 0x1FF;
    nextCode_ = 0x100;
    std::fill(prefix_.begin(), prefix_.end(), std::uint16_t{0xFFFF});
    for (int i = 0; i < 256; ++i) suffix_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
}

std::uint8_t PicDecoder::nextByte() {
    if (pending_.empty()) {
        // Read a code: the unused high bits of the last word, then whole words.
        std::uint32_t acc = static_cast<std::uint32_t>(input_) >> (16 - inputBits_);
        int have = inputBits_;
        while (have < bits_) {
            input_ = nextWord();
            acc |= static_cast<std::uint32_t>(input_) << have;
            have += 16;
        }
        inputBits_ = have - bits_;
        const int code = static_cast<int>(acc) & mask_;

        // A code not in the table yet (the KwKwK case) is the previous string
        // plus its own first byte.
        int newPrev = code, walk = code;
        if (code >= nextCode_) {
            newPrev = nextCode_;
            walk = prevCode_;
            pending_.push_back(firstChar_);
        }
        for (int guard = 0; prefix_[static_cast<std::size_t>(walk)] != 0xFFFF; ++guard) {
            if (guard >= kTableSize) throw std::runtime_error("PIC: corrupt LZW data (loop)");
            pending_.push_back(suffix_[static_cast<std::size_t>(walk)]);
            walk = prefix_[static_cast<std::size_t>(walk)];
            if (walk >= kTableSize) throw std::runtime_error("PIC: corrupt LZW data (code)");
        }
        firstChar_ = suffix_[static_cast<std::size_t>(walk)];
        pending_.push_back(firstChar_);

        // The new entry is the previous string plus this one's first byte. The
        // first code after a reset also adds one (with a stale prefix), as the
        // original does; the encoder skips that code number.
        if (nextCode_ < kTableSize) {
            suffix_[static_cast<std::size_t>(nextCode_)] = firstChar_;
            prefix_[static_cast<std::size_t>(nextCode_)] = static_cast<std::uint16_t>(prevCode_);
        }
        if (++nextCode_ > mask_) {
            ++bits_;
            mask_ = mask_ << 1 | 1;
        }
        if (bits_ > maxBits_) resetTable();
        prevCode_ = newPrev;
    }
    const std::uint8_t b = pending_.back();
    pending_.pop_back();
    return b;
}

void PicDecoder::decode(std::span<std::uint8_t> out, int count, bool nibbles) {
    const int n = nibbles ? (count + 1) / 2 : count;
    if (static_cast<std::size_t>(nibbles ? n * 2 : n) > out.size())
        throw std::logic_error("PicDecoder::decode: output too small");
    for (int i = 0; i < n; ++i) {
        std::uint8_t b;
        if (run_ == 0) {
            b = nextByte();
            if (b == 0x90) {
                const std::uint8_t len = nextByte();
                if (len == 0) {
                    runByte_ = 0x90;  // 90h 00: a literal 90h
                } else {
                    // 90h n: n-1 more copies of the previous byte, this one
                    // included. (Byte arithmetic: 90h 01 wraps to 256 copies,
                    // as in the original; the encoder never writes it.)
                    run_ = static_cast<std::uint8_t>(len - 2);
                    b = runByte_;
                }
            } else {
                runByte_ = b;
            }
        } else {
            b = runByte_;
            --run_;
        }
        if (nibbles) {
            out[static_cast<std::size_t>(i) * 2] = b & 15;
            out[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::uint8_t>(b >> 4);
        } else {
            out[static_cast<std::size_t>(i)] = b;
        }
    }
}

Picture decodePicture(std::span<const std::uint8_t> data, std::size_t pos, std::size_t* end) {
    if (pos + 8 > data.size()) throw std::runtime_error("PIC: truncated header");
    Picture pic;
    PicHeader& h = pic.header;
    h.flags = wordAt(data, pos);
    h.width = wordAt(data, pos + 2);
    h.height = wordAt(data, pos + 4);
    pos += 6;
    if (h.width <= 0 || h.height <= 0 || h.width > 2048 || h.height > 2048)
        throw std::runtime_error("PIC: bad size " + std::to_string(h.width) + "x" + std::to_string(h.height));
    // pic_begin_decode: bit 3 wins over bit 4.
    const std::size_t mapSize = (h.flags & 8) ? 16 : (h.flags & 0x10) ? 128 : 0;
    if (pos + mapSize > data.size()) throw std::runtime_error("PIC: truncated colour map");
    h.colorMap.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                      data.begin() + static_cast<std::ptrdiff_t>(pos + mapSize));
    pos += mapSize;

    PicDecoder dec(data, pos);
    pic.image = Image(h.width, h.height);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(h.width) + 1);
    for (int y = 0; y < h.height; ++y) {
        dec.decode(row, h.width, h.nibbles());
        std::copy_n(row.begin(), h.width, pic.image.row(y));
    }
    if (end) *end = dec.position();
    return pic;
}

}  // namespace reaction
