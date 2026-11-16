#include "app/PickerScene.h"

#include <algorithm>
#include <system_error>
#include <utility>

#include "audio/Synth.h"
#include "core/BuiltinFont.h"
#include "core/GameData.h"
#include "core/Folders.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"

namespace reaction {

namespace fs = std::filesystem;
namespace bf = builtin_font;
namespace gs = folders;

namespace {

// Colours (game palette): the text windows' black box with a grey edge, white
// text, the yellow selection bar.
constexpr std::uint8_t kBlack = 0, kBox = 5, kEdge = 7, kGrey = 7, kDarkGrey = 8, kWhite = 15,
                       kYellow = 14, kCyan = 11, kGreen = 10, kRed = 12;

// Layout (320x200, 8x8 cells).
constexpr int kHeaderY = 2, kPathY = 64, kListY = 74, kRowY = 77, kRows = 11, kStatusY = 175,
              kButtonY = 188;
constexpr int kListX = 4, kListW = 311, kTextX = 10, kNameCols = 36, kArrowX = 304;
constexpr int kUseX = 6, kUseW = 8 * 18 + 5, kExitW = 8 * 9 + 5, kExitX = 315 - kExitW, kButtonH = 10;

// The help at the top, per mode (two title lines, then four).
constexpr const char* kGameHeader[] = {
    "Choose the game files' folder:",
    "Sid Meier's Covert Action (1990).",
    "The folder with TEXT.DTA, from GOG's",
    "release or the original disks.",
    "Folders that hold the game files are",
    "marked in green.",
};
constexpr const char* kRomHeader[] = {
    "Choose the MT-32 ROMs' folder:",
    "Roland MT-32 or CM-32L ROM dumps.",
    "The folder with the control ROM and",
    "the PCM ROM (e.g. MT32_CONTROL.ROM",
    "and MT32_PCM.ROM). Folders with a",
    "usable pair are marked in green.",
};

bool inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x <= rx + rw && y <= ry + rh;
}

std::string displayName(const fs::path& p) { return bf::fromUtf8(gs::toUtf8(p)); }

}  // namespace

bool PickerScene::holds(Mode mode, const fs::path& dir) {
    if (dir.empty()) return false;
    if (mode == Mode::GameFiles) return GameData::isGameDir(dir);
    return audio::findMt32Roms({dir}).has_value();
}

PickerScene::PickerScene(const Screen& screen, fs::path start, Mode mode) : screen_(screen), mode_(mode) {
    std::error_code ec;
    if (start.empty() || !fs::is_directory(start, ec)) start = gs::homeDir();
    if (start.empty() || !fs::is_directory(start, ec)) start = fs::current_path(ec);
    enter(gs::tidy(start));
}

void PickerScene::enter(const fs::path& dir, const fs::path& select) {
    cur_ = dir;
    items_.clear();
    message_.clear();
    std::vector<gs::Entry> list;
    if (cur_.empty()) {
        list = gs::drives();
    } else {
#ifdef _WIN32
        items_.push_back({Kind::Up, "..", {}});   // up to the drives from a root
#else
        if (!gs::isRoot(cur_)) items_.push_back({Kind::Up, "..", {}});
#endif
        list = gs::subfolders(cur_);
    }
    for (auto& e : list) items_.push_back({Kind::Folder, bf::fromUtf8(e.name), std::move(e.path)});
    curValid_ = !cur_.empty() && holds(mode_, cur_);
    sel_ = top_ = 0;
    if (!select.empty())
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
            if (items_[static_cast<std::size_t>(i)].kind == Kind::Folder &&
                items_[static_cast<std::size_t>(i)].path == select)
                sel_ = i;
    moveTo(sel_);
}

void PickerScene::goUp() {
    if (cur_.empty()) return;
    const fs::path from = cur_;
    if (gs::isRoot(cur_)) {
#ifdef _WIN32
        enter({}, from);
#endif
        return;
    }
    enter(cur_.parent_path(), from);
}

bool PickerScene::valid(int index, bool ask) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return false;
    Item& it = items_[static_cast<std::size_t>(index)];
    if (it.kind != Kind::Folder) return false;
    if (it.valid < 0 && !ask && gs::needsPermission(it.path)) return false;
    if (it.valid < 0) it.valid = holds(mode_, it.path) ? 1 : 0;
    return it.valid == 1;
}

void PickerScene::open(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    const Item& it = items_[static_cast<std::size_t>(index)];
    if (it.kind == Kind::Up) return goUp();
    if (valid(index, true)) {
        chosen_ = it.path;
        return;
    }
    const fs::path p = it.path;
    enter(p);
}

void PickerScene::use() {
    if (valid(sel_, true)) chosen_ = items_[static_cast<std::size_t>(sel_)].path;
    else if (cur_.empty()) say("Choose a drive first.", kRed);
    else chosen_ = cur_;   // (the setup screen says what it lacks)
}

void PickerScene::moveTo(int index) {
    const int n = static_cast<int>(items_.size());
    sel_ = n == 0 ? 0 : std::clamp(index, 0, n - 1);
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
    top_ = std::clamp(top_, 0, std::max(0, n - kRows));
}

void PickerScene::say(std::string text, std::uint8_t color) {
    message_ = std::move(text);
    messageColor_ = color;
}

int PickerScene::rowAt(int x, int y) const {
    if (x < kListX || x > kListX + kListW - 12 || y < kRowY || y >= kRowY + kRows * 8) return -1;
    const int i = top_ + (y - kRowY) / 8;
    return i < static_cast<int>(items_.size()) ? i : -1;
}

void PickerScene::handle(const SDL_Event& e) {
    if (chosen_) return;
    if (e.type == SDL_KEYDOWN) {
        message_.clear();
        switch (e.key.keysym.sym) {
        case SDLK_UP: case SDLK_KP_8: moveTo(sel_ - 1); break;
        case SDLK_DOWN: case SDLK_KP_2: moveTo(sel_ + 1); break;
        case SDLK_PAGEUP: case SDLK_KP_9: moveTo(sel_ - (kRows - 1)); break;
        case SDLK_PAGEDOWN: case SDLK_KP_3: moveTo(sel_ + (kRows - 1)); break;
        case SDLK_HOME: case SDLK_KP_7: moveTo(0); break;
        case SDLK_END: case SDLK_KP_1: moveTo(static_cast<int>(items_.size()) - 1); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_RIGHT: open(sel_); break;
        case SDLK_BACKSPACE: case SDLK_LEFT: goUp(); break;
        case SDLK_u: use(); break;
        case SDLK_ESCAPE: quit = true; break;
        default: break;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        const int n = static_cast<int>(items_.size());
        top_ = std::clamp(top_ - 3 * e.wheel.y, 0, std::max(0, n - kRows));
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const SDL_Point p = screen_.toFrame(e.button.x, e.button.y);
        if (const int row = rowAt(p.x, p.y); row >= 0) {
            if (row == sel_ || e.button.clicks >= 2) open(row);
            else moveTo(row);
        } else if (inside(p.x, p.y, kArrowX, kRowY, 8, 8)) {
            top_ = std::max(0, top_ - (kRows - 1));
        } else if (inside(p.x, p.y, kArrowX, kRowY + (kRows - 1) * 8, 8, 8)) {
            top_ = std::clamp(top_ + (kRows - 1), 0, std::max(0, static_cast<int>(items_.size()) - kRows));
        } else if (inside(p.x, p.y, kUseX, kButtonY, kUseW, kButtonH)) {
            use();
        } else if (inside(p.x, p.y, kExitX, kButtonY, kExitW, kButtonH)) {
            quit = true;
        }
    }
}

void PickerScene::draw(Frame& frame) {
    Image& img = frame.image;
    frame.palette = mcgaPalette(palettes::kGame);
    img.fill(kBlack);

    // What's needed.
    const bool roms = mode_ == Mode::Mt32Roms;
    const char* const* header = roms ? kRomHeader : kGameHeader;
    drawBevelBox(img, kListX, kHeaderY, kListW, 6 * 8 + 10, kBox);
    for (int i = 0; i < 6; ++i)   // (a gap between the title and the help)
        bf::drawText(img, kTextX - 2, kHeaderY + 3 + i * 8 + (i < 2 ? 0 : 4), header[i], i < 2 ? kYellow : kWhite);

    // Where we are.
    const std::string where = cur_.empty() ? std::string("Drives") : displayName(cur_);
    const int x = bf::drawText(img, kTextX - 2, kPathY, "Folder: ", kGrey);
    bf::drawText(img, x, kPathY, bf::fitLeft(where, (312 - x) / 8), kCyan);

    // The folders.
    drawBevelBox(img, kListX, kListY, kListW, kRows * 8 + 5, kBox);
    const int n = static_cast<int>(items_.size());
    if (n == 0) bf::drawText(img, kTextX, kRowY, cur_.empty() ? "(no drives)" : "(no folders)", kDarkGrey);
    for (int row = 0; row < kRows && top_ + row < n; ++row) {
        const int i = top_ + row;
        const int y = kRowY + row * 8;
        const bool isSel = i == sel_;
        const bool ok = valid(i);
        const Item& it = items_[static_cast<std::size_t>(i)];
        if (isSel) fillRect(img, kListX + 2, y, kArrowX - kListX - 4, 8, kYellow);
        const std::uint8_t c = isSel ? kBlack : ok ? kGreen : kWhite;
        std::string label = it.kind == Kind::Up ? std::string("..") : it.label;
        if (it.kind == Kind::Folder && !cur_.empty()) label += "/";
        if (ok) {
            bf::drawText(img, kTextX, y, bf::fitRight(label, kNameCols - 14), c);
            bf::drawText(img, kTextX + (kNameCols - 13) * 8, y, roms ? "<- MT-32 ROMs" : "<- game files",
                         isSel ? kBlack : kGreen);
        } else {
            bf::drawText(img, kTextX, y, bf::fitRight(label, kNameCols), c);
        }
    }
    if (top_ > 0) bf::drawText(img, kArrowX, kRowY, std::string(1, bf::kArrowUp), kWhite);
    if (top_ + kRows < n)
        bf::drawText(img, kArrowX, kRowY + (kRows - 1) * 8, std::string(1, bf::kArrowDown), kWhite);

    // What to do.
    const bool found = valid(sel_) || curValid_;
    const std::string what = roms ? "MT-32 ROMs" : "Game files";
    std::string status = message_;
    std::uint8_t statusColor = messageColor_;
    if (status.empty()) {
        if (valid(sel_)) status = what + " found: press Enter or U.", statusColor = kGreen;
        else if (curValid_) status = what + " here: press U to use them.", statusColor = kGreen;
        else status = "Enter: open  Backspace: up  Esc: back", statusColor = kGrey;
    }
    bf::drawText(img, kListX, kStatusY, bf::fitRight(status, 39), statusColor);

    const bool canUse = !cur_.empty() || valid(sel_);
    drawRectOutline(img, kUseX, kButtonY, kUseW, kButtonH, found ? kGreen : canUse ? kEdge : kDarkGrey);
    bf::drawText(img, kUseX + 3, kButtonY + 2, "U: Use this folder", canUse ? kWhite : kDarkGrey);
    drawRectOutline(img, kExitX, kButtonY, kExitW, kButtonH, kEdge);
    bf::drawText(img, kExitX + 3, kButtonY + 2, "Esc: Back", kWhite);
}

}  // namespace reaction
