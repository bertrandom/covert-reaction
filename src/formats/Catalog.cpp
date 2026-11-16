#include "formats/Catalog.h"

#include <cctype>

namespace redux {

std::optional<std::size_t> catalogFind(std::span<const std::uint8_t> cat, std::string_view name) {
    if (cat.size() < 2) return std::nullopt;
    const int count = cat[0] | cat[1] << 8;
    for (int i = 0; i < count; ++i) {
        const std::size_t e = 2 + static_cast<std::size_t>(i) * 0x18;
        if (e + 0x18 > cat.size()) break;
        // strnicmp(entry, name, 12): stops at the first difference or NUL.
        bool same = true;
        for (std::size_t k = 0; k < 12; ++k) {
            const int a = std::tolower(cat[e + k]);
            const int b = k < name.size() ? std::tolower(static_cast<unsigned char>(name[k])) : 0;
            if (a != b) { same = false; break; }
            if (a == 0) break;
        }
        if (same)
            return static_cast<std::size_t>(cat[e + 20] | cat[e + 21] << 8 | cat[e + 22] << 16 |
                                            static_cast<std::uint32_t>(cat[e + 23]) << 24);
    }
    return std::nullopt;
}

}  // namespace redux
