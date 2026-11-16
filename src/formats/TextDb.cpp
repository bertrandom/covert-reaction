#include "formats/TextDb.h"

namespace reaction {

TextDb TextDb::parse(std::span<const std::uint8_t> bytes) {
    TextDb db;
    std::string line;
    auto flush = [&] {
        // fscanf's trailing "\n" directive skips all white space before the next
        // line, so leading blanks never reach the string and blank lines vanish.
        std::size_t start = line.find_first_not_of(" \t\f\v");
        if (start != std::string::npos) db.lines_.push_back(line.substr(start));
        line.clear();
    };
    for (std::uint8_t b : bytes) {
        if (b == '\n') flush();
        else if (b != '\r' && b != 0x1A) line.push_back(static_cast<char>(b));  // CR LF, DOS EOF
    }
    flush();
    return db;
}

std::optional<std::string> TextDb::lookup(std::string_view key,
                                          std::span<const Substitution> subst) const {
    std::size_t i = 0;
    while (i < lines_.size() && lines_[i] != key) ++i;
    if (i == lines_.size()) return std::nullopt;
    ++i;
    while (i < lines_.size() && lines_[i].starts_with('*')) ++i;   // aliases
    std::string out;
    for (; i < lines_.size() && !lines_[i].starts_with('*'); ++i) {
        std::string s = lines_[i];
        for (const auto& [from, to] : subst) {
            if (auto p = s.find(from); p != std::string::npos) s.replace(p, from.size(), to);
        }
        out += s;
        out += ' ';
    }
    return out;
}

std::vector<std::string> TextDb::keys() const {
    std::vector<std::string> out;
    bool prevKey = false;
    for (const auto& l : lines_) {
        bool k = l.starts_with('*');
        if (k && !prevKey) out.push_back(l);
        prevKey = k;
    }
    return out;
}

}  // namespace reaction
