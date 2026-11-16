#include "formats/Archive.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string_view>

#include <miniz.h>

extern "C" {
#include <7z.h>
#include <7zCrc.h>
#include <Lzma2Dec.h>
#include <LzmaDec.h>
}

namespace reaction::archive {

namespace {

using Span = std::span<const std::uint8_t>;

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

std::uint16_t le16(Span d, std::size_t p) {
    if (p + 2 > d.size()) fail("unexpected end of data");
    return static_cast<std::uint16_t>(d[p] | d[p + 1] << 8);
}
std::uint32_t le32(Span d, std::size_t p) {
    if (p + 4 > d.size()) fail("unexpected end of data");
    return static_cast<std::uint32_t>(d[p]) | static_cast<std::uint32_t>(d[p + 1]) << 8 |
           static_cast<std::uint32_t>(d[p + 2]) << 16 | static_cast<std::uint32_t>(d[p + 3]) << 24;
}
std::uint64_t be(Span d, std::size_t p, int n) {
    if (p + static_cast<std::size_t>(n) > d.size()) fail("unexpected end of data");
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = v << 8 | d[p + static_cast<std::size_t>(i)];
    return v;
}

Span slice(Span d, std::uint64_t off, std::uint64_t len) {
    if (off > d.size() || len > d.size() - off) fail("unexpected end of data");
    return d.subspan(static_cast<std::size_t>(off), static_cast<std::size_t>(len));
}

Bytes copyOf(Span s) { return Bytes(s.begin(), s.end()); }

// The LZMA SDK's allocator.
void* szAlloc(ISzAllocPtr, size_t size) { return size ? std::malloc(size) : nullptr; }
void szFree(ISzAllocPtr, void* p) { std::free(p); }
const ISzAlloc kAlloc = {szAlloc, szFree};

// LzmaDec / Lzma2Dec, the same loop: decode until the input ends (no progress),
// the end mark, or `limit`.
template <typename Dec, typename Step>
Bytes lzmaLoop(Dec& dec, Span in, std::size_t limit, Step step) {
    Bytes out(std::min(limit, std::max<std::size_t>(in.size() * 4, std::size_t(1) << 16)));
    std::size_t done = 0, pos = 0;
    for (;;) {
        if (done == out.size()) {
            if (out.size() >= limit) break;
            out.resize(std::min(limit, out.size() * 2));
        }
        SizeT outLen = out.size() - done, inLen = in.size() - pos;
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        const SRes r = step(dec, out.data() + done, &outLen, in.data() + pos, &inLen, &status);
        done += outLen;
        pos += inLen;
        if (r != SZ_OK) fail("LZMA data error");
        if (status == LZMA_STATUS_FINISHED_WITH_MARK) break;
        if (outLen == 0 && inLen == 0) break;
    }
    out.resize(done);
    return out;
}

}  // namespace

// ---- Streams -------------------------------------------------------------------

Bytes inflate(Span in, bool zlib, std::size_t size) {
    mz_stream s{};
    if (mz_inflateInit2(&s, zlib ? MZ_DEFAULT_WINDOW_BITS : -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
        fail("inflate: can't start");
    struct End {
        mz_stream* s;
        ~End() { mz_inflateEnd(s); }
    } end{&s};
    Bytes out(size ? size : std::max<std::size_t>(in.size() * 3, 4096));
    std::size_t done = 0, pos = 0;
    for (;;) {
        if (done == out.size()) out.resize(out.size() * 2);
        const std::size_t inChunk = std::min<std::size_t>(in.size() - pos, 1u << 30);
        const std::size_t outChunk = std::min<std::size_t>(out.size() - done, 1u << 30);
        s.next_in = in.data() + pos;
        s.avail_in = static_cast<unsigned>(inChunk);
        s.next_out = out.data() + done;
        s.avail_out = static_cast<unsigned>(outChunk);
        const int r = mz_inflate(&s, MZ_NO_FLUSH);
        pos += inChunk - s.avail_in;
        done += outChunk - s.avail_out;
        if (r == MZ_STREAM_END) break;
        if (r != MZ_OK && r != MZ_BUF_ERROR) fail("inflate: bad data");
        if (s.avail_out != 0 && pos == in.size()) fail("inflate: data cut short");
    }
    out.resize(done);
    return out;
}

Bytes gunzip(Span in) {
    if (!isGzip(in) || in.size() < 18 || in[2] != 8) fail("not gzip data");
    const std::uint8_t flags = in[3];
    std::size_t p = 10;
    if (flags & 4) p += 2 + le16(in, p);                        // FEXTRA
    for (int bit : {8, 16})                                     // FNAME, FCOMMENT
        if (flags & bit) {
            while (p < in.size() && in[p]) ++p;
            ++p;
        }
    if (flags & 2) p += 2;                                      // FHCRC
    if (p >= in.size()) fail("gzip: data cut short");
    const std::uint32_t isize = le32(in, in.size() - 4);
    return inflate(in.subspan(p), false, isize > 0 && isize < (1u << 30) ? isize : 0);
}

Bytes lzmaDecode(Span in, std::size_t limit) {
    if (in.size() < 5) fail("LZMA: no properties");
    CLzmaDec dec;
    LzmaDec_Construct(&dec);
    if (LzmaDec_Allocate(&dec, in.data(), 5, &kAlloc) != SZ_OK) fail("LZMA: bad properties");
    struct Free {
        CLzmaDec* d;
        ~Free() { LzmaDec_Free(d, &kAlloc); }
    } guard{&dec};
    LzmaDec_Init(&dec);
    return lzmaLoop(dec, in.subspan(5), limit, [](CLzmaDec& d, Byte* o, SizeT* ol, const Byte* i, SizeT* il,
                                                  ELzmaStatus* st) {
        return LzmaDec_DecodeToBuf(&d, o, ol, i, il, LZMA_FINISH_ANY, st);
    });
}

Bytes lzma2Decode(Span in, std::size_t limit) {
    if (in.empty()) fail("LZMA2: no properties");
    CLzma2Dec dec;
    Lzma2Dec_Construct(&dec);
    if (Lzma2Dec_Allocate(&dec, in[0], &kAlloc) != SZ_OK) fail("LZMA2: bad properties");
    struct Free {
        CLzma2Dec* d;
        ~Free() { Lzma2Dec_Free(d, &kAlloc); }
    } guard{&dec};
    Lzma2Dec_Init(&dec);
    return lzmaLoop(dec, in.subspan(1), limit, [](CLzma2Dec& d, Byte* o, SizeT* ol, const Byte* i, SizeT* il,
                                                  ELzmaStatus* st) {
        return Lzma2Dec_DecodeToBuf(&d, o, ol, i, il, LZMA_FINISH_ANY, st);
    });
}

std::uint32_t crc32(Span data, std::uint32_t crc) {
    return static_cast<std::uint32_t>(mz_crc32(crc, data.data(), data.size()));
}

// ---- Recognizing -----------------------------------------------------------------

bool isGzip(Span h) { return h.size() >= 2 && h[0] == 0x1f && h[1] == 0x8b; }
bool isXar(Span h) { return h.size() >= 4 && std::memcmp(h.data(), "xar!", 4) == 0; }
bool is7z(Span h) {
    static const std::uint8_t kSig[] = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
    return h.size() >= 6 && std::memcmp(h.data(), kSig, 6) == 0;
}
bool isFatImage(Span h) {
    if (h.size() < 512 || h[510] != 0x55 || h[511] != 0xaa) return false;
    const unsigned bps = le16(h, 11);
    return bps == 512 || bps == 1024;
}

namespace {

// The end-of-central-directory record: its offset, or npos.
std::size_t findEocd(Span f) {
    if (f.size() < 22) return std::string::npos;
    const std::size_t stop = f.size() > 22 + 65535 ? f.size() - 22 - 65535 : 0;
    for (std::size_t p = f.size() - 22;; --p) {
        if (f[p] == 'P' && f[p + 1] == 'K' && f[p + 2] == 5 && f[p + 3] == 6) return p;
        if (p == stop) break;
    }
    return std::string::npos;
}

}  // namespace

bool hasZip(Span f) { return findEocd(f) != std::string::npos; }

// ---- zip -------------------------------------------------------------------------

Entries zipEntries(Shared file) {
    const Span f(*file);
    const std::size_t eocd = findEocd(f);
    if (eocd == std::string::npos) fail("not a zip archive");
    const std::uint32_t count = le16(f, eocd + 10), cdSize = le32(f, eocd + 12), cdOffset = le32(f, eocd + 16);
    if (cdSize == 0xffffffff || cdOffset == 0xffffffff) fail("zip64 archives aren't supported");
    if (cdSize > eocd) fail("zip: bad central directory");
    // Where the zip starts in the file: after a script or an installer, its own
    // offsets are relative to that (as Python's zipfile works it out).
    const std::size_t cdStart = eocd - cdSize;
    if (cdOffset > cdStart) fail("zip: bad central directory");
    const std::size_t base = cdStart - cdOffset;
    Entries out;
    std::size_t p = cdStart;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (le32(f, p) != 0x02014b50) fail("zip: bad central directory entry");
        const unsigned flags = le16(f, p + 8), method = le16(f, p + 10);
        const std::uint32_t csize = le32(f, p + 20), usize = le32(f, p + 24);
        const unsigned nlen = le16(f, p + 28), elen = le16(f, p + 30), clen = le16(f, p + 32);
        const std::uint32_t local = le32(f, p + 42);
        const Span nameBytes = slice(f, p + 46, nlen);
        std::string name(nameBytes.begin(), nameBytes.end());
        p += 46 + nlen + elen + clen;
        if (name.empty() || name.back() == '/') continue;   // a folder
        if (csize == 0xffffffff || usize == 0xffffffff || local == 0xffffffff)
            fail("zip64 archives aren't supported");
        out.push_back({name, [file, base, local, flags, method, csize, usize, name] {
                           const Span z(*file);
                           const std::size_t lh = base + local;
                           if (le32(z, lh) != 0x04034b50) fail("zip: bad local header for " + name);
                           if (flags & 1) fail("zip: " + name + " is encrypted");
                           const Span data = slice(z, lh + 30 + le16(z, lh + 26) + le16(z, lh + 28), csize);
                           Bytes b;
                           if (method == 0) b = copyOf(data);
                           else if (method == 8) b = inflate(data, false, usize);
                           else fail("zip: " + name + ": unsupported compression method " + std::to_string(method));
                           if (b.size() != usize) fail("zip: " + name + ": wrong size");
                           return b;
                       }});
    }
    return out;
}

// ---- xar -------------------------------------------------------------------------

namespace {

// Just enough XML for xar's table of contents: elements, attributes, text.
struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::string text;
    std::vector<XmlNode> children;

    const XmlNode* child(std::string_view n) const {
        for (const auto& c : children)
            if (c.name == n) return &c;
        return nullptr;
    }
    std::string childText(std::string_view n) const {
        const XmlNode* c = child(n);
        return c ? c->text : std::string();
    }
};

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t semi = s.find(';', i);
        if (semi == std::string_view::npos) {
            out += s[i];
            continue;
        }
        const std::string_view e = s.substr(i + 1, semi - i - 1);
        if (e == "amp") out += '&';
        else if (e == "lt") out += '<';
        else if (e == "gt") out += '>';
        else if (e == "quot") out += '"';
        else if (e == "apos") out += '\'';
        else if (!e.empty() && e[0] == '#') {
            const unsigned long c = e.size() > 1 && (e[1] == 'x' || e[1] == 'X')
                                        ? std::strtoul(std::string(e.substr(2)).c_str(), nullptr, 16)
                                        : std::strtoul(std::string(e.substr(1)).c_str(), nullptr, 10);
            if (c < 0x80) {
                out += static_cast<char>(c);
            } else if (c < 0x800) {
                out += static_cast<char>(0xc0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3f));
            } else {
                out += static_cast<char>(0xe0 | ((c >> 12) & 0x0f));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
        } else {
            out += '&';
            continue;
        }
        i = semi;
    }
    return out;
}

class XmlParser {
public:
    explicit XmlParser(std::string_view s) : s_(s) {}

    XmlNode parse() {
        XmlNode root;
        root.name = "#document";
        parseContent(root);
        return root;
    }

private:
    void parseContent(XmlNode& node) {
        while (p_ < s_.size()) {
            if (s_[p_] != '<') {
                const std::size_t lt = s_.find('<', p_);
                node.text += unescape(s_.substr(p_, lt - p_));
                p_ = lt == std::string_view::npos ? s_.size() : lt;
                continue;
            }
            if (s_.compare(p_, 4, "<!--") == 0) {
                skipPast("-->");
            } else if (s_.compare(p_, 9, "<![CDATA[") == 0) {
                const std::size_t end = s_.find("]]>", p_);
                if (end == std::string_view::npos) fail("xar: bad table of contents");
                node.text += std::string(s_.substr(p_ + 9, end - p_ - 9));
                p_ = end + 3;
            } else if (s_.compare(p_, 2, "<?") == 0 || s_.compare(p_, 2, "<!") == 0) {
                skipPast(">");
            } else if (s_.compare(p_, 2, "</") == 0) {
                skipPast(">");
                return;   // (the end tag of `node`)
            } else {
                node.children.push_back(parseElement());
            }
        }
    }

    XmlNode parseElement() {
        XmlNode n;
        ++p_;   // '<'
        n.name = name();
        for (;;) {
            skipSpace();
            if (p_ >= s_.size()) fail("xar: bad table of contents");
            if (s_[p_] == '/') {
                skipPast(">");
                return n;
            }
            if (s_[p_] == '>') {
                ++p_;
                parseContent(n);
                return n;
            }
            std::string key = name();
            skipSpace();
            if (p_ >= s_.size() || s_[p_] != '=') fail("xar: bad table of contents");
            ++p_;
            skipSpace();
            if (p_ >= s_.size() || (s_[p_] != '"' && s_[p_] != '\'')) fail("xar: bad table of contents");
            const char q = s_[p_++];
            const std::size_t end = s_.find(q, p_);
            if (end == std::string_view::npos) fail("xar: bad table of contents");
            n.attrs[key] = unescape(s_.substr(p_, end - p_));
            p_ = end + 1;
        }
    }

    std::string name() {
        const std::size_t start = p_;
        while (p_ < s_.size() && !std::strchr(" \t\r\n/>=", s_[p_])) ++p_;
        if (p_ == start) fail("xar: bad table of contents");
        return std::string(s_.substr(start, p_ - start));
    }
    void skipSpace() {
        while (p_ < s_.size() && std::strchr(" \t\r\n", s_[p_])) ++p_;
    }
    void skipPast(std::string_view end) {
        const std::size_t e = s_.find(end, p_);
        p_ = e == std::string_view::npos ? s_.size() : e + end.size();
    }

    std::string_view s_;
    std::size_t p_ = 0;
};

void xarWalk(const XmlNode& el, const std::string& prefix, const Shared& file, std::uint64_t heap,
             Entries& out) {
    for (const auto& f : el.children) {
        if (f.name != "file") continue;
        const std::string path = prefix + f.childText("name");
        if (const XmlNode* d = f.child("data")) {
            const std::uint64_t off = std::strtoull(d->childText("offset").c_str(), nullptr, 10);
            const std::uint64_t len = std::strtoull(d->childText("length").c_str(), nullptr, 10);
            const std::uint64_t size = std::strtoull(d->childText("size").c_str(), nullptr, 10);
            std::string enc;
            if (const XmlNode* e = d->child("encoding"); e && e->attrs.count("style")) enc = e->attrs.at("style");
            out.push_back({path, [file, heap, off, len, size, enc, path] {
                               const Span raw = slice(*file, heap + off, len);
                               if (enc.find("gzip") != std::string::npos)
                                   return inflate(raw, true, size < (1u << 30) ? static_cast<std::size_t>(size) : 0);
                               if (enc.find("bzip2") != std::string::npos)
                                   fail("xar: " + path + " is bzip2-compressed (not supported)");
                               return copyOf(raw);
                           }});
        }
        xarWalk(f, path + "/", file, heap, out);
    }
}

}  // namespace

Entries xarEntries(Shared file) {
    const Span f(*file);
    if (!isXar(f)) fail("not a xar archive");
    const std::uint64_t hsize = be(f, 4, 2), clen = be(f, 8, 8), ulen = be(f, 16, 8);
    const Bytes tocXml = inflate(slice(f, hsize, clen), true, ulen < (1u << 30) ? static_cast<std::size_t>(ulen) : 0);
    const XmlNode doc =
        XmlParser(std::string_view(reinterpret_cast<const char*>(tocXml.data()), tocXml.size())).parse();
    const XmlNode* xar = doc.child("xar");
    const XmlNode* toc = xar ? xar->child("toc") : nullptr;
    if (!toc) fail("xar: no table of contents");
    Entries out;
    xarWalk(*toc, "", file, hsize + clen, out);
    return out;
}

// ---- cpio ------------------------------------------------------------------------

namespace {

std::size_t number(Span d, std::size_t p, std::size_t n, int base) {
    const Span s = slice(d, p, n);
    const std::string str(s.begin(), s.end());
    char* end = nullptr;
    const unsigned long long v = std::strtoull(str.c_str(), &end, base);
    if (end != str.c_str() + str.size() || v > SIZE_MAX / 2) fail("cpio: bad header");
    return static_cast<std::size_t>(v);
}

}  // namespace

Entries cpioEntries(Shared file) {
    const Span d(*file);
    Entries out;
    std::size_t p = 0;
    while (p + 6 <= d.size()) {
        const std::string_view magic(reinterpret_cast<const char*>(d.data() + p), 6);
        std::size_t mode, nlen, fsize;
        std::string name;
        std::size_t body;
        if (magic == "070707") {
            mode = number(d, p + 18, 6, 8);
            nlen = number(d, p + 59, 6, 8);
            fsize = number(d, p + 65, 11, 8);
            const Span n = slice(d, p + 76, nlen ? nlen - 1 : 0);
            name.assign(n.begin(), n.end());
            body = p + 76 + nlen;
            p = body + fsize;
        } else if (magic == "070701" || magic == "070702") {
            mode = number(d, p + 14, 8, 16);
            fsize = number(d, p + 54, 8, 16);
            nlen = number(d, p + 94, 8, 16);
            const Span n = slice(d, p + 110, nlen ? nlen - 1 : 0);
            name.assign(n.begin(), n.end());
            body = (p + 110 + nlen + 3) & ~std::size_t(3);
            p = (body + fsize + 3) & ~std::size_t(3);
        } else {
            break;
        }
        if (name == "TRAILER!!!") break;
        (void)slice(d, body, fsize);   // (inside the archive)
        if ((mode & 0170000) == 0100000) {   // a regular file
            if (name.rfind("./", 0) == 0) name.erase(0, 2);
            out.push_back({name, [file, body, fsize] { return copyOf(slice(*file, body, fsize)); }});
        }
    }
    return out;
}

// ---- FAT -------------------------------------------------------------------------

namespace {

struct Fat {
    Span img;
    unsigned bps = 0, spc = 0, res = 0, nfats = 0, nroot = 0, spf = 0;
    std::size_t root = 0, data = 0, clus = 0;

    explicit Fat(Span image) : img(image) {
        if (img.size() < 512) fail("not a FAT image");
        bps = le16(img, 11);
        spc = img[13];
        res = le16(img, 14);
        nfats = img[16];
        nroot = le16(img, 17);
        spf = le16(img, 22);
        if ((bps != 512 && bps != 1024) || !spc || !nfats) fail("not a FAT image");
        root = static_cast<std::size_t>(res + nfats * spf) * bps;
        data = root + static_cast<std::size_t>((nroot * 32 + bps - 1) / bps) * bps;
        clus = static_cast<std::size_t>(spc) * bps;
    }

    unsigned next(unsigned c) const {
        const std::size_t fat = static_cast<std::size_t>(res) * bps, at = fat + c * 3 / 2;
        if (at + 2 > fat + static_cast<std::size_t>(spf) * bps || at + 2 > img.size()) return 0xfff;
        const unsigned v = le16(img, at);
        return c & 1 ? v >> 4 : v & 0xfff;
    }

    Bytes chain(unsigned c) const {
        Bytes out;
        std::set<unsigned> seen;
        while (c >= 2 && c < 0xff0 && seen.insert(c).second) {
            const std::size_t at = data + (c - 2) * clus;
            if (at < img.size()) {
                const std::size_t n = std::min(clus, img.size() - at);
                out.insert(out.end(), img.begin() + static_cast<std::ptrdiff_t>(at),
                           img.begin() + static_cast<std::ptrdiff_t>(at + n));
            }
            c = next(c);
        }
        return out;
    }

    void walk(Span raw, const std::string& prefix, const Shared& file, Entries& out, int depth) const {
        for (std::size_t k = 0; k + 32 <= raw.size(); k += 32) {
            const Span e = raw.subspan(k, 32);
            if (e[0] == 0) break;
            if (e[0] == 0xe5 || (e[11] & 0x08) || e[11] == 0x0f) continue;   // deleted, label, long name
            auto field = [&](std::size_t at, std::size_t n) {
                std::string s(e.begin() + static_cast<std::ptrdiff_t>(at),
                              e.begin() + static_cast<std::ptrdiff_t>(at + n));
                while (!s.empty() && s.back() == ' ') s.pop_back();
                return s;
            };
            const std::string base = field(0, 8), ext = field(8, 3);
            if (base == "." || base == "..") continue;
            const std::string name = base + (ext.empty() ? "" : "." + ext);
            const unsigned start = le16(e, 26);
            const std::uint32_t size = le32(e, 28);
            if (e[11] & 0x10) {
                if (depth < 16) {
                    const Bytes sub = chain(start);
                    walk(sub, prefix + name + "/", file, out, depth + 1);
                }
            } else {
                out.push_back({prefix + name, [file, start, size] {
                                   Bytes b = Fat(*file).chain(start);
                                   if (b.size() > size) b.resize(size);
                                   return b;
                               }});
            }
        }
    }
};

}  // namespace

Entries fatEntries(Shared image) {
    const Fat fat(*image);
    Entries out;
    fat.walk(slice(*image, fat.root, static_cast<std::uint64_t>(fat.nroot) * 32), "", image, out, 0);
    return out;
}

// ---- 7z --------------------------------------------------------------------------

namespace {

struct MemStream {
    ISeekInStream vt;
    const Bytes* data;
    std::size_t pos;
};

SRes memRead(ISeekInStreamPtr p, void* buf, size_t* size) {
    auto* s = Z7_CONTAINER_FROM_VTBL_SIMPLE(p, MemStream, vt);
    const std::size_t n = std::min(*size, s->data->size() - std::min(s->pos, s->data->size()));
    if (n) std::memcpy(buf, s->data->data() + s->pos, n);
    s->pos += n;
    *size = n;
    return SZ_OK;
}

SRes memSeek(ISeekInStreamPtr p, Int64* pos, ESzSeek origin) {
    auto* s = Z7_CONTAINER_FROM_VTBL_SIMPLE(p, MemStream, vt);
    Int64 base = 0;
    if (origin == SZ_SEEK_CUR) base = static_cast<Int64>(s->pos);
    else if (origin == SZ_SEEK_END) base = static_cast<Int64>(s->data->size());
    const Int64 to = base + *pos;
    if (to < 0) return SZ_ERROR_PARAM;
    s->pos = static_cast<std::size_t>(to);
    *pos = to;
    return SZ_OK;
}

// An open archive, shared by its entries' readers. SzArEx_Extract keeps the
// last solid block it unpacked, so reading the files in order unpacks each once.
struct SevenZip {
    Shared file;
    MemStream stream{};
    CLookToRead2 look{};
    CSzArEx db{};
    UInt32 blockIndex = 0xffffffff;
    Byte* outBuffer = nullptr;
    size_t outBufferSize = 0;
    std::mutex lock;

    explicit SevenZip(Shared f) : file(std::move(f)) {
        static std::once_flag crcTable;
        std::call_once(crcTable, [] { CrcGenerateTable(); });
        stream.vt.Read = memRead;
        stream.vt.Seek = memSeek;
        stream.data = file.get();
        LookToRead2_CreateVTable(&look, False);
        look.buf = static_cast<Byte*>(std::malloc(1 << 16));
        if (!look.buf) fail("out of memory");
        look.bufSize = 1 << 16;
        look.realStream = &stream.vt;
        LookToRead2_INIT(&look);
        SzArEx_Init(&db);
        if (SzArEx_Open(&db, &look.vt, &kAlloc, &kAlloc) != SZ_OK) {
            release();
            fail("not a 7z archive, or a damaged one");
        }
    }
    ~SevenZip() { release(); }
    SevenZip(const SevenZip&) = delete;
    SevenZip& operator=(const SevenZip&) = delete;

    void release() {
        SzArEx_Free(&db, &kAlloc);
        std::free(outBuffer);
        outBuffer = nullptr;
        std::free(look.buf);
        look.buf = nullptr;
    }

    std::string name(UInt32 i) const {
        const size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        std::vector<UInt16> u(len);
        SzArEx_GetFileNameUtf16(&db, i, u.data());
        std::string out;
        for (size_t k = 0; k < len && u[k]; ++k) {
            unsigned c = u[k];
            if (c >= 0xd800 && c < 0xdc00 && k + 1 < len && u[k + 1] >= 0xdc00 && u[k + 1] < 0xe000)
                c = 0x10000 + ((c - 0xd800) << 10) + (u[++k] - 0xdc00u);
            if (c == '\\') c = '/';
            if (c < 0x80) {
                out += static_cast<char>(c);
            } else if (c < 0x800) {
                out += static_cast<char>(0xc0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3f));
            } else if (c < 0x10000) {
                out += static_cast<char>(0xe0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            } else {
                out += static_cast<char>(0xf0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        return out;
    }

    Bytes extract(UInt32 i) {
        std::lock_guard<std::mutex> g(lock);
        size_t offset = 0, size = 0;
        if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuffer, &outBufferSize, &offset, &size, &kAlloc,
                           &kAlloc) != SZ_OK)
            fail("7z: can't unpack " + name(i));
        return Bytes(outBuffer + offset, outBuffer + offset + size);
    }
};

}  // namespace

Entries sevenZipEntries(Shared file) {
    if (!is7z(*file)) fail("not a 7z archive");
    auto arc = std::make_shared<SevenZip>(std::move(file));
    Entries out;
    for (UInt32 i = 0; i < arc->db.NumFiles; ++i) {
        if (SzArEx_IsDir(&arc->db, i)) continue;
        out.push_back({arc->name(i), [arc, i] { return arc->extract(i); }});
    }
    return out;
}

}  // namespace reaction::archive
