#include "app/SetupScene.h"

#include <cstdio>
#include <system_error>
#include <utility>

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include "app/MacFolderPanel.h"
#define REACTION_MAC_PANEL 1
#endif
#include "core/BuiltinFont.h"
#include "core/Folders.h"
#include "core/GameData.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"

namespace reaction {

namespace fs = std::filesystem;
namespace bf = builtin_font;

namespace {

// Colours (game palette), as PickerScene's.
constexpr std::uint8_t kBlack = 0, kBox = 5, kEdge = 7, kGrey = 7, kDarkGrey = 8, kWhite = 15,
                       kYellow = 14, kCyan = 11, kGreen = 10, kRed = 12;

// Layout (320x200): the help, the two rows, a note, the bottom buttons.
constexpr int kBoxX = 4, kBoxW = 311, kTextX = 8, kCols = 37;
constexpr int kHelpY = 2, kHelpH = 7 * 8 + 4 + 6;
constexpr int kGameY = 72, kRomY = 124, kRowH = 48;   // each row: label, two lines, button
constexpr int kNoteY = 176, kBottomY = 187, kButtonH = 11;
constexpr int kChooseW = 8 * 20 + 6, kStartW = 8 * 7 + 6, kQuitW = 8 * 11 + 6;

struct Rect {
    int x, y, w, h;
};

constexpr Rect kRects[] = {
    {kTextX, kGameY + 34, kChooseW, kButtonH},
    {kTextX, kRomY + 34, kChooseW, kButtonH},
    {kTextX, kBottomY, kStartW, kButtonH},
    {312 - kQuitW, kBottomY, kQuitW, kButtonH},
};

constexpr const char* kHelp[] = {
    "Covert Reaction needs the files of",
    "Sid Meier's Covert Action (1990): a",
    "GOG installer, eXoDOS zip, the disks",
    "or its folder (they're copied). MT-32",
    "ROMs are optional: they play the",
    "Roland MT-32 music (else the AdLib).",
};

std::string displayName(const fs::path& p) { return bf::fromUtf8(folders::toUtf8(p)); }

bool isDir(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

// `p`, or its nearest folder that exists (empty if none).
fs::path existing(fs::path p) {
    for (; !p.empty(); p = p.parent_path()) {
        if (isDir(p)) return p;
        if (p == p.parent_path()) break;
    }
    return {};
}

}  // namespace

SetupScene::SetupScene(const Screen& screen, SetupConfig config, Done done)
    : screen_(screen), config_(std::move(config)), done_(std::move(done)),
      game_(config_.gameDir), roms_(config_.romDir) {
    refresh();
    focus_ = gameOk_ ? kStart : kChooseGame;
}

void SetupScene::refresh() {
    gameMissing_ = game_.empty() ? std::vector<std::string>{} : GameData::missingFiles(game_);
    gameOk_ = !game_.empty() && gameMissing_.empty();
    romSet_.reset();
    romFoundIn_.clear();
    if (!roms_.empty()) {
        romSet_ = audio::findMt32Roms({roms_});
    } else if (config_.autoRomDirs) {
        romSet_ = audio::findMt32Roms(config_.autoRomDirs(gameOk_ ? game_ : fs::path()));
        if (romSet_ && !romSet_->control.files.empty()) romFoundIn_ = romSet_->control.files[0].parent_path();
    }
    if (!enabled(focus_)) focus_ = kChooseGame;
}

bool SetupScene::enabled(int button) const { return !job_ && (button != kStart || gameOk_); }

void SetupScene::focusNext(int step) {
    for (int i = 0; i < kButtons; ++i) {
        focus_ = (focus_ + step + kButtons) % kButtons;
        if (enabled(focus_)) return;
    }
}

int SetupScene::buttonAt(int x, int y) const {
    for (int i = 0; i < kButtons; ++i) {
        const Rect& r = kRects[i];
        if (x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h) return i;
    }
    return -1;
}

void SetupScene::press(int button) {
    if (!enabled(button)) return;
    switch (button) {
    case kChooseGame: choose(PickerScene::Mode::GameFiles); break;
    case kChooseRoms: choose(PickerScene::Mode::Mt32Roms); break;
    case kStart: accepted_ = true; break;
    case kQuit:
        if (config_.inGame) cancelled_ = true;
        else quit = true;
        break;
    default: break;
    }
}

void SetupScene::choose(PickerScene::Mode mode) {
    const bool roms = mode == PickerScene::Mode::Mt32Roms;
    // Where to start: near the folder chosen so far; for the ROMs, else next to the game.
    fs::path start = existing(roms ? roms_ : !importFrom_.empty() ? importFrom_.parent_path() : game_);
    if (start.empty() && roms && gameOk_) start = game_.parent_path();
    if (!roms && !config_.browserStart.empty() && !config_.nativePanel) start = config_.browserStart;
#ifdef REACTION_MAC_PANEL
    if (config_.nativePanel) {
        const char* message =
            roms ? "Choose the folder containing your MT-32 (or CM-32L) control ROM and PCM ROM files, "
                   "e.g. MT32_CONTROL.ROM and MT32_PCM.ROM."
                 : "Choose Sid Meier's Covert Action (1990): GOG's installer (.exe, .pkg or .sh), eXoDOS's .zip, "
                   "the original disks (a .7z or a disk image), or a folder that holds the game. Its files are "
                   "copied into Covert Reaction's own folder.";
        auto dir = roms || config_.importDir.empty() ? macChooseFolder(start, message)
                                                     : macChooseFileOrFolder(start, message, importExtensions());
        if (dir) take(mode, *dir);
        return;
    }
#endif
    if (roms && start.empty()) start = config_.browserStart;
    picker_ = std::make_unique<PickerScene>(screen_, start, mode);
    pickerMode_ = mode;
}

void SetupScene::take(PickerScene::Mode mode, const fs::path& dir) {
    const bool wasOk = gameOk_;
    if (mode == PickerScene::Mode::GameFiles && !config_.importDir.empty()) {
        std::error_code ec;
        if (fs::equivalent(dir, config_.importDir, ec) && GameData::isGameDir(dir)) {
            game_ = config_.importDir;   // (the copy imported before)
        } else {
            // Import it (a folder too): the game then uses its own copy.
            importFrom_ = dir;
            importError_.clear();
            job_ = std::make_unique<ImportJob>(dir, config_.importDir, !config_.syncImport);
            if (job_->done()) finishImport();
            return;
        }
    } else if (mode == PickerScene::Mode::GameFiles) {
        game_ = dir;
    } else {
        roms_ = dir;
    }
    refresh();
    if (mode == PickerScene::Mode::GameFiles && gameOk_ && !wasOk) focus_ = kStart;
}

void SetupScene::finishImport() {
    const ImportResult r = job_->result();
    job_.reset();
    if (r.ok) {
        std::fprintf(stderr, "imported %zu files from %s (%s)\n", r.files, folders::toUtf8(importFrom_).c_str(),
                     describeGameSource(r.source));
        game_ = config_.importDir;
        imported_ = true;
    } else {
        std::fprintf(stderr, "import from %s failed: %s\n", folders::toUtf8(importFrom_).c_str(), r.error.c_str());
        importError_ = r.error;
    }
    refresh();
    focus_ = r.ok && gameOk_ ? kStart : kChooseGame;
}

void SetupScene::tick() {
    if (job_ && job_->done()) finishImport();
}

void SetupScene::handle(const SDL_Event& e) {
    if (accepted_ || cancelled_ || job_) return;
    if (picker_) {
        picker_->handle(e);
        if (picker_->chosen()) {
            const fs::path dir = *picker_->chosen();
            picker_.reset();
            take(pickerMode_, dir);
        } else if (picker_->quit) {
            picker_.reset();
        }
        return;
    }
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
        case SDLK_UP: case SDLK_KP_8: focusNext(-1); break;
        case SDLK_DOWN: case SDLK_KP_2: focusNext(1); break;
        case SDLK_TAB: focusNext((e.key.keysym.mod & KMOD_SHIFT) ? -1 : 1); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: press(focus_); break;
        case SDLK_ESCAPE: press(kQuit); break;
        default: break;
        }
    } else if (e.type == SDL_MOUSEMOTION) {
        const SDL_Point p = screen_.toFrame(e.motion.x, e.motion.y);
        if (const int b = buttonAt(p.x, p.y); b >= 0 && enabled(b)) focus_ = b;
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const SDL_Point p = screen_.toFrame(e.button.x, e.button.y);
        if (const int b = buttonAt(p.x, p.y); b >= 0 && enabled(b)) {
            focus_ = b;
            press(b);
        }
    }
}

std::unique_ptr<Scene> SetupScene::next() {
    if (!config_.inGame || !done_ || !(accepted_ || cancelled_)) return nullptr;
    auto d = std::move(done_);
    done_ = nullptr;
    return d(accepted_, *this);
}

void SetupScene::draw(Frame& frame) {
    if (picker_) return picker_->draw(frame);
    Image& img = frame.image;
    frame.palette = mcgaPalette(palettes::kGame);
    img.fill(kBlack);

    // What's needed.
    drawBevelBox(img, kBoxX, kHelpY, kBoxW, kHelpH, kBox);
    bf::drawText(img, kTextX, kHelpY + 3, config_.inGame ? "Game files and ROMs" : "Covert Reaction setup", kYellow);
    for (int i = 0; i < 6; ++i) bf::drawText(img, kTextX, kHelpY + 3 + 12 + i * 8, kHelp[i], kWhite);

    // One row: its label, two lines of status.
    auto row = [&](int y, const char* label, const std::string& line1, std::uint8_t c1, const std::string& line2,
                   std::uint8_t c2) {
        drawBevelBox(img, kBoxX, y, kBoxW, kRowH, kBox);
        bf::drawText(img, kTextX, y + 4, label, kYellow);
        bf::drawText(img, kTextX, y + 14, line1, c1);
        bf::drawText(img, kTextX, y + 24, line2, c2);
    };

    // The game files.
    {
        std::string l1 = "Not chosen", l2;
        std::uint8_t c1 = kGrey, c2 = kGreen;
        if (job_) {
            l1 = "Importing " + bf::fitLeft(displayName(importFrom_), kCols - 10);
            c1 = kCyan;
            l2 = bf::fitRight(std::to_string(static_cast<int>(job_->progress() * 100)) + "%  " + job_->stage(), kCols);
            c2 = kYellow;
        } else if (!importError_.empty()) {
            l1 = bf::fitLeft(displayName(importFrom_), kCols);
            c1 = kCyan;
            l2 = bf::fitRight(importError_, kCols);
            c2 = kRed;
        } else if (!game_.empty()) {
            l1 = bf::fitLeft(displayName(game_), kCols);
            c1 = kCyan;
            if (!isDir(game_)) {
                l2 = "Missing: the folder is gone";
                c2 = kRed;
            } else if (!gameOk_) {
                l2 = "Missing:";
                for (const auto& m : gameMissing_) l2 += " " + m;
                l2 = bf::fitRight(l2, kCols);
                c2 = kRed;
            } else {
                l2 = "Covert Action's files: OK";
                if (imported_) l1 = "From " + bf::fitLeft(displayName(importFrom_), kCols - 5);
            }
        }
        row(kGameY, "Game files", l1, c1, l2, c2);
    }

    // The MT-32 ROMs.
    {
        std::string l1 = "Not chosen", l2 = "The music plays on the AdLib.";
        std::uint8_t c1 = kGrey, c2 = kGrey;
        if (!roms_.empty()) {
            l1 = bf::fitLeft(displayName(roms_), kCols);
            c1 = kCyan;
            if (romSet_) l2 = bf::fitRight(romSet_->describe(), kCols), c2 = kGreen;
            else if (!isDir(roms_)) l2 = "That folder is gone.", c2 = kRed;
            else l2 = "No usable control + PCM ROM pair.", c2 = kRed;
        } else if (romSet_) {
            l1 = "Found: " + bf::fitLeft(displayName(romFoundIn_), kCols - 7);
            l2 = bf::fitRight(romSet_->describe(), kCols);
            c2 = kGreen;
        }
        row(kRomY, "MT-32 ROMs (optional)", l1, c1, l2, c2);
    }

    // A note.
    std::string note = "Tab/arrows: move  Enter: choose";
    std::uint8_t noteColor = kDarkGrey;
    if (config_.inGame && gameChanged()) note = "A new game folder applies next start.", noteColor = kYellow;
    bf::drawText(img, kBoxX, kNoteY, bf::fitRight(note, 39), noteColor);

    // The buttons.
    const char* labels[] = {"Choose game files...", "Choose MT-32 ROMs...", config_.inGame ? "Done" : "START",
                            config_.inGame ? "Esc: Cancel" : "Esc: Quit"};
    for (int i = 0; i < kButtons; ++i) {
        const Rect& r = kRects[i];
        const bool on = enabled(i), focused = on && i == focus_;
        if (focused) fillRect(img, r.x + 1, r.y + 1, r.w - 1, r.h - 1, kYellow);
        drawRectOutline(img, r.x, r.y, r.w, r.h, !on ? kDarkGrey : focused ? kYellow : i == kStart ? kGreen : kEdge);
        const int tx = r.x + (r.w + 1 - bf::textWidth(labels[i])) / 2;
        bf::drawText(img, tx, r.y + 2, labels[i], focused ? kBlack : on ? kWhite : kDarkGrey);
    }
}

}  // namespace reaction
