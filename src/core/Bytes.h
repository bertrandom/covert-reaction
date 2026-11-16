// Little-endian readers/writers for the original's binary files (16-bit ints).
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace redux {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> d, std::size_t pos = 0) : d_(d), p_(pos) {}

    std::uint8_t u8() { need(1); return d_[p_++]; }
    std::int8_t s8() { return static_cast<std::int8_t>(u8()); }
    std::uint16_t u16() {
        need(2);
        auto v = static_cast<std::uint16_t>(d_[p_] | d_[p_ + 1] << 8);
        p_ += 2;
        return v;
    }
    std::int16_t s16() { return static_cast<std::int16_t>(u16()); }
    // A fixed-size NUL-padded string field.
    std::string str(std::size_t n) {
        need(n);
        std::string s(reinterpret_cast<const char*>(d_.data() + p_), n);
        p_ += n;
        if (auto z = s.find('\0'); z != std::string::npos) s.resize(z);
        return s;
    }
    void skip(std::size_t n) { need(n); p_ += n; }
    std::size_t pos() const { return p_; }
    std::size_t left() const { return d_.size() - p_; }

private:
    void need(std::size_t n) const {
        if (p_ + n > d_.size()) throw std::runtime_error("unexpected end of data");
    }
    std::span<const std::uint8_t> d_;
    std::size_t p_;
};

class ByteWriter {
public:
    void u8(std::uint8_t v) { b_.push_back(v); }
    void s8(std::int8_t v) { u8(static_cast<std::uint8_t>(v)); }
    void u16(std::uint16_t v) { b_.push_back(v & 0xFF); b_.push_back(static_cast<std::uint8_t>(v >> 8)); }
    void s16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void str(const std::string& s, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) u8(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
    }
    void zeros(std::size_t n) { b_.insert(b_.end(), n, 0); }
    std::vector<std::uint8_t>& bytes() { return b_; }

private:
    std::vector<std::uint8_t> b_;
};

}  // namespace redux
