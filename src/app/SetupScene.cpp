#include "app/SetupScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string_view>
#include <system_error>
#include <utility>

#include "app/FileDialog.h"
#include "core/Folders.h"
#include "core/GameData.h"
#include "core/UiFont.h"

namespace reaction {

namespace fs = std::filesystem;

namespace {

// ---- The look ------------------------------------------------------------------

namespace color {
constexpr Color kBgTop = Color::hex(0x151B27), kBgBottom = Color::hex(0x0A0D12);
constexpr Color kGlow = Color::hex(0x3D6FD9, 34);
constexpr Color kCard = Color::hex(0x181E29), kCardBorder = Color::hex(0x273040);
constexpr Color kText = Color::hex(0xEAEEF4), kText2 = Color::hex(0xA4AEBC), kText3 = Color::hex(0x6F7988);
constexpr Color kAccent = Color::hex(0x4C8DFF), kAccentHover = Color::hex(0x6AA0FF), kAccentDown = Color::hex(0x3B77E3);
constexpr Color kGreen = Color::hex(0x3CCB7F), kAmber = Color::hex(0xF2B13C), kRed = Color::hex(0xFF6B6B);
constexpr Color kButton = Color::hex(0x232A37), kButtonHover = Color::hex(0x2C3546), kButtonDown = Color::hex(0x1E2430);
constexpr Color kButtonBorder = Color::hex(0x364052), kDisabled = Color::hex(0x1C222C), kDisabledText = Color::hex(0x586272);
constexpr Color kFocus = Color::hex(0x8DB6FF, 220);
constexpr Color kTrack = Color::hex(0x262E3C);
constexpr Color kIconInk = Color::hex(0x10141B);
}  // namespace color

// Sizes in points (x the UI scale).
constexpr float kMaxContent = 820, kMargin = 40, kNarrowMargin = 20;
constexpr float kTitlePx = 26, kSubtitlePx = 14.5f, kCardTitlePx = 16.5f, kBodyPx = 14, kSmallPx = 12.5f,
                kButtonPx = 14, kBadgePx = 10.5f;
constexpr float kButtonH = 36, kFooterButtonH = 40, kButtonPad = 18, kButtonRadius = 8, kButtonGap = 10;
constexpr float kCardPad = 20, kCardRadius = 14, kCardGap = 14, kIcon = 28;

constexpr const char* kHelp =
    "Works with GOG’s installers for Windows, macOS and Linux, the eXoDOS zip, the floppy disk images, "
    "or a folder with the game. The game’s files are copied into Covert Reaction’s own folder; "
    "nothing is installed.";

enum class Status { Empty, Ok, Warning, Error, Busy };

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

// A path for the player: the home folder as "~" (not on Windows).
std::string displayPath(const fs::path& p) {
    std::string s = folders::toUtf8(p);
#ifndef _WIN32
    const std::string home = folders::toUtf8(folders::homeDir());
    if (!home.empty() && home != "/" && s.rfind(home, 0) == 0 && (s.size() == home.size() || s[home.size()] == '/'))
        s = "~" + s.substr(home.size());
#endif
    return s;
}

// A message as a sentence: a capital first, a full stop last.
std::string sentence(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s;
}

std::string fileName(const fs::path& p) {
    const fs::path name = p.filename().empty() ? p.parent_path().filename() : p.filename();
    return folders::toUtf8(name.empty() ? p : name);
}

// ---- Drawing -------------------------------------------------------------------

// A line of text in a box of height `lh` whose top is `y`: the capitals centred.
float baselineIn(const UiFont& f, float y, float lh, float px) { return std::round(y + (lh + f.capHeight(px)) / 2); }

void drawIcon(Canvas& c, Status st, float x, float y, float size, float spin) {
    const float cx = x + size / 2, cy = y + size / 2, r = size / 2;
    auto at = [&](float fx, float fy) { return std::pair{x + fx * size, y + fy * size}; };
    switch (st) {
    case Status::Ok: {
        c.fillCircle(cx, cy, r, color::kGreen);
        auto [ax, ay] = at(0.29f, 0.52f);
        auto [bx, by] = at(0.44f, 0.67f);
        auto [qx, qy] = at(0.72f, 0.37f);
        c.line(ax, ay, bx, by, size * 0.11f, color::kIconInk);
        c.line(bx, by, qx, qy, size * 0.11f, color::kIconInk);
        break;
    }
    case Status::Warning:
    case Status::Error: {
        const Color fill = st == Status::Error ? color::kRed : color::kAmber;
        const float rr = size * 0.08f;
        auto [ax, ay] = at(0.5f, 0.08f + 0.06f);
        auto [bx, by] = at(0.96f - 0.07f, 0.9f - 0.04f);
        auto [qx, qy] = at(0.04f + 0.07f, 0.9f - 0.04f);
        c.fillTriangle(ax, ay, bx, by, qx, qy, rr, fill);
        c.line(cx, y + size * 0.38f, cx, y + size * 0.60f, size * 0.1f, color::kIconInk);
        c.fillCircle(cx, y + size * 0.74f, size * 0.06f, color::kIconInk);
        break;
    }
    case Status::Busy: {
        const float th = size * 0.11f;
        c.strokeCircle(cx, cy, r * 0.9f, th, color::kTrack);
        c.strokeArc(cx, cy, r * 0.9f, spin, spin + 1.7f, th, color::kAccent);
        break;
    }
    case Status::Empty:
    default: c.strokeCircle(cx, cy, r * 0.9f, size * 0.075f, color::kText3); break;
    }
}

// The header's emblem: a crosshair in a rounded square.
void drawEmblem(Canvas& c, float x, float y, float size) {
    c.fillRoundRect({x, y, size, size}, size * 0.26f, Color::hex(0x1C2433));
    c.strokeRoundRect({x, y, size, size}, size * 0.26f, std::max(1.f, size * 0.025f), Color::hex(0x2E3A50));
    const float cx = x + size / 2, cy = y + size / 2, r = size * 0.27f, th = size * 0.055f;
    c.strokeCircle(cx, cy, r, th, color::kAccent);
    const float in = r * 0.45f, out = r * 1.45f;
    c.line(cx, cy - out, cx, cy - in, th, color::kAccent);
    c.line(cx, cy + in, cx, cy + out, th, color::kAccent);
    c.line(cx - out, cy, cx - in, cy, th, color::kAccent);
    c.line(cx + in, cy, cx + out, cy, th, color::kAccent);
    c.fillCircle(cx, cy, th * 0.9f, color::kText);
}

}  // namespace

// ---- The scene -------------------------------------------------------------------

SetupScene::SetupScene(const Screen& screen, SetupConfig config, Done done)
    : screen_(screen), config_(std::move(config)), done_(std::move(done)),
      game_(config_.gameDir), roms_(config_.romDir) {
    const UiFonts& fonts = systemUiFonts();
    regular_ = fonts.regular.get();
    bold_ = fonts.bold.get();
    // The buttons, in focus order: the game's, the ROMs', then Quit and START.
    const dialogs::Kind kind = dialogs::kind();
    std::fprintf(stderr, "file dialogs: %s\n", dialogs::describe(kind));
    if (kind == dialogs::Kind::None || dialogs::takesFileOrFolder(kind) || config_.importDir.empty()) {
        buttons_.push_back({Action::ChooseGame, "Choose…"});
    } else {
        buttons_.push_back({Action::ChooseGameFile, "Choose installer or archive…"});
        buttons_.push_back({Action::ChooseGameFolder, "Choose folder…"});
    }
    if (config_.romImportDir.empty()) {
        buttons_.push_back({Action::ChooseRoms, "Choose folder…"});
    } else if (kind == dialogs::Kind::None || dialogs::takesFileOrFolder(kind)) {
        buttons_.push_back({Action::ChooseRoms, "Choose…"});
    } else {
        buttons_.push_back({Action::ChooseRomFile, "Choose archive…"});
        buttons_.push_back({Action::ChooseRomFolder, "Choose folder…"});
    }
    buttons_.push_back({Action::ClearRoms, "Clear"});
    buttons_.push_back({Action::Quit, config_.inGame ? "Cancel" : "Quit"});
    buttons_.push_back({Action::Start, config_.inGame ? "Done" : "START", true});
    refresh();
    focus_ = gameOk_ ? indexOf(Action::Start) : 0;
}

SetupScene::~SetupScene() = default;

void SetupScene::refresh() {
    gameMissing_ = game_.empty() ? std::vector<std::string>{} : GameData::missingFiles(game_);
    gameOk_ = !game_.empty() && gameMissing_.empty();
    // (A damaged copy is shown, not refused: START still works.)
    gameCheck_ = gameOk_ ? checkGameFiles(game_) : GameFileCheck{};
    romSet_.reset();
    romFoundIn_.clear();
    if (!roms_.empty()) {
        romSet_ = audio::findMt32Roms({roms_});
    } else if (config_.autoRomDirs) {
        romSet_ = audio::findMt32Roms(config_.autoRomDirs(gameOk_ ? game_ : fs::path()));
        if (romSet_ && !romSet_->control.files.empty()) romFoundIn_ = romSet_->control.files[0].parent_path();
    }
    if (focus_ >= static_cast<int>(buttons_.size()) || !enabled(buttons_[static_cast<std::size_t>(focus_)].action))
        focus_ = 0;
    dirty_ = true;
}

bool SetupScene::enabled(Action a) const {
    if (job_) return false;
    if (a == Action::Start) return gameOk_;
    if (a == Action::ClearRoms) return !roms_.empty() || !romError_.empty();
    return true;
}

int SetupScene::romChooseIndex() const {
    for (Action a : {Action::ChooseRoms, Action::ChooseRomFile, Action::ChooseRomFolder})
        if (const int i = indexOf(a); i >= 0) return i;
    return 0;
}

int SetupScene::indexOf(Action a) const {
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i].action == a) return static_cast<int>(i);
    return 0;
}

void SetupScene::focusNext(int step) {
    const int n = static_cast<int>(buttons_.size());
    for (int i = 0; i < n; ++i) {
        focus_ = (focus_ + step + n) % n;
        if (enabled(buttons_[static_cast<std::size_t>(focus_)].action)) break;
    }
    dirty_ = true;
}

int SetupScene::buttonAt(float x, float y) const {
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i].rect.contains(x, y)) return static_cast<int>(i);
    return -1;
}

void SetupScene::press(Action a) {
    if (!enabled(a)) return;
    dirty_ = true;
    switch (a) {
    case Action::ChooseGame:
    case Action::ChooseGameFile:
    case Action::ChooseGameFolder:
    case Action::ChooseRoms:
    case Action::ChooseRomFile:
    case Action::ChooseRomFolder: choose(a); break;
    case Action::ClearRoms:
        roms_.clear();
        romError_.clear();
        romFrom_.clear();
        refresh();
        focus_ = romChooseIndex();
        break;
    case Action::Start: accepted_ = true; break;
    case Action::Quit:
        if (config_.inGame) cancelled_ = true;
        else quit = true;
        break;
    }
}

void SetupScene::choose(Action a) {
    const bool roms = a == Action::ChooseRoms || a == Action::ChooseRomFile || a == Action::ChooseRomFolder;
    const PickerScene::Mode mode = roms ? PickerScene::Mode::Mt32Roms : PickerScene::Mode::GameFiles;
    // Where to start: near what was chosen so far; for the ROMs, else next to the game.
    fs::path start = existing(roms ? (!romFrom_.empty() ? romFrom_.parent_path() : roms_) : !importFrom_.empty() ? importFrom_.parent_path() : game_);
    if (start.empty() && roms && gameOk_) start = game_.parent_path();

    std::optional<fs::path> picked;
    if (dialogs::takeTestPick(picked)) {   // (tests: COVERT_TEST_PICK)
        if (picked) take(mode, *picked);
        return;
    }
    if (dialogs::kind() == dialogs::Kind::None) {
        // No system dialog: the in-game browser.
        if (!roms && !config_.browserStart.empty()) start = config_.browserStart;
        if (start.empty()) start = config_.browserStart;
        picker_ = std::make_unique<PickerScene>(screen_, start, mode);
        pickerMode_ = mode;
        return;
    }
    if (!config_.dialogs) {
        std::fprintf(stderr, "setup: no file dialog in a scripted run (set COVERT_TEST_PICK)\n");
        return;
    }
    dialogs::Request r;
    r.start = start;
    r.extensions = importExtensions();
    r.filterName = "Covert Action releases";
    switch (a) {
    case Action::ChooseRoms:
    case Action::ChooseRomFile:
    case Action::ChooseRomFolder:
        r.extensions = audio::mt32RomExtensions();
        r.filterName = "MT-32 ROMs";
        r.pick = config_.romImportDir.empty() || a == Action::ChooseRomFolder ? dialogs::Pick::Folder
                 : a == Action::ChooseRomFile                                 ? dialogs::Pick::File
                                                                              : dialogs::Pick::FileOrFolder;
        r.title = r.pick == dialogs::Pick::Folder ? "Choose the folder with your MT-32 ROMs"
                                                  : "Choose your MT-32 ROMs";
        r.message = config_.romImportDir.empty()
                        ? "Choose the folder containing your MT-32 (or CM-32L) control ROM and PCM ROM files, "
                          "e.g. MT32_CONTROL.ROM and MT32_PCM.ROM."
                        : "Choose your MT-32 (or CM-32L) control and PCM ROMs: a .zip or .7z that holds them, "
                          "a folder, or one of the ROM files. They are copied into Covert Reaction's own folder.";
        break;
    case Action::ChooseGameFile:
        r.pick = dialogs::Pick::File;
        r.title = "Choose Covert Action’s installer, archive or disk image";
        break;
    case Action::ChooseGameFolder:
        r.pick = dialogs::Pick::Folder;
        r.title = "Choose the folder with Covert Action (or its disk images)";
        break;
    default:
        r.pick = config_.importDir.empty() ? dialogs::Pick::Folder : dialogs::Pick::FileOrFolder;
        r.title = "Choose Sid Meier’s Covert Action";
        r.message = "Choose Sid Meier's Covert Action (1990): GOG's installer (.exe, .pkg or .sh), eXoDOS's .zip, "
                    "the original disks (a .7z or a disk image), or a folder that holds the game. Its files are "
                    "copied into Covert Reaction's own folder.";
        break;
    }
    picked = dialogs::choose(r, screen_.window());
    if (picked) take(mode, *picked);
}

void SetupScene::take(PickerScene::Mode mode, const fs::path& path) {
    const bool wasOk = gameOk_;
    dirty_ = true;
    if (mode == PickerScene::Mode::GameFiles && !config_.importDir.empty()) {
        std::error_code ec;
        if (fs::equivalent(path, config_.importDir, ec) && GameData::isGameDir(path)) {
            game_ = config_.importDir;   // (the copy imported before)
            importFrom_.clear();
            importError_.clear();
        } else {
            // Import it (a folder too): the game then uses its own copy.
            importFrom_ = path;
            importError_.clear();
            job_ = std::make_unique<ImportJob>(path, config_.importDir, !config_.syncImport);
            if (job_->done()) finishImport();
            return;
        }
    } else if (mode == PickerScene::Mode::GameFiles) {
        game_ = path;
    } else if (!config_.romImportDir.empty()) {
        // Copy the best pair into the per-user folder (a few MB: no thread).
        const audio::Mt32ImportResult r = audio::importMt32Roms(path, config_.romImportDir);
        romFrom_ = path;
        if (r.ok) {
            std::fprintf(stderr, "imported the MT-32 ROMs (%s) from %s\n", r.roms->describe().c_str(),
                         folders::toUtf8(path).c_str());
            roms_ = config_.romImportDir;
            romError_.clear();
        } else {
            std::fprintf(stderr, "MT-32 ROM import from %s failed: %s\n", folders::toUtf8(path).c_str(),
                         r.error.c_str());
            romError_ = r.error;
        }
    } else {
        roms_ = path;
    }
    refresh();
    if (mode == PickerScene::Mode::GameFiles && gameOk_ && !wasOk) focus_ = indexOf(Action::Start);
}

void SetupScene::finishImport() {
    const ImportResult r = job_->result();
    job_.reset();
    if (r.ok) {
        std::fprintf(stderr, "imported %zu files from %s (%s)\n", r.files, folders::toUtf8(importFrom_).c_str(),
                     describeGameSource(r.source));
        game_ = config_.importDir;
        imported_ = true;
        lastImport_ = r;
    } else {
        std::fprintf(stderr, "import from %s failed: %s\n", folders::toUtf8(importFrom_).c_str(), r.error.c_str());
        importError_ = r.error;
    }
    refresh();
    focus_ = r.ok && gameOk_ ? indexOf(Action::Start) : 0;
}

void SetupScene::tick() {
    ++ticks_;
    if (job_) {
        dirty_ = true;   // (the progress, the spinner)
        if (job_->done()) finishImport();
    }
}

void SetupScene::handle(const SDL_Event& e) {
    if (accepted_ || cancelled_) return;
    if (picker_) {
        picker_->handle(e);
        if (picker_->chosen()) {
            const fs::path p = *picker_->chosen();
            picker_.reset();
            take(pickerMode_, p);
        } else if (picker_->quit) {
            picker_.reset();
        }
        dirty_ = true;
        return;
    }
    if (e.type == SDL_WINDOWEVENT) {
        dirty_ = true;
        return;
    }
    if (job_) return;
    const float ratio = screen_.pixelRatio();
    switch (e.type) {
    case SDL_KEYDOWN:
        focusVisible_ = true;
        dirty_ = true;
        switch (e.key.keysym.sym) {
        case SDLK_UP: case SDLK_LEFT: case SDLK_KP_8: case SDLK_KP_4: focusNext(-1); break;
        case SDLK_DOWN: case SDLK_RIGHT: case SDLK_KP_2: case SDLK_KP_6: focusNext(1); break;
        case SDLK_TAB: focusNext((e.key.keysym.mod & KMOD_SHIFT) ? -1 : 1); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
            if (!(e.key.keysym.mod & KMOD_ALT)) press(buttons_[static_cast<std::size_t>(focus_)].action);
            break;
        case SDLK_ESCAPE: press(Action::Quit); break;
        default: break;
        }
        break;
    case SDL_MOUSEMOTION: {
        const int b = buttonAt(static_cast<float>(e.motion.x) * ratio, static_cast<float>(e.motion.y) * ratio);
        if (b != hover_) {
            hover_ = b;
            dirty_ = true;
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            pressed_ = buttonAt(static_cast<float>(e.button.x) * ratio, static_cast<float>(e.button.y) * ratio);
            if (pressed_ >= 0 && !enabled(buttons_[static_cast<std::size_t>(pressed_)].action)) pressed_ = -1;
            dirty_ = true;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) {
            const int b = buttonAt(static_cast<float>(e.button.x) * ratio, static_cast<float>(e.button.y) * ratio);
            const int was = pressed_;
            pressed_ = -1;
            dirty_ = true;
            if (b >= 0 && b == was) {
                focus_ = b;
                focusVisible_ = false;
                press(buttons_[static_cast<std::size_t>(b)].action);
            }
        }
        break;
    default: break;
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
    frame.image.fill(0);
}

bool SetupScene::drawCanvas(Canvas& canvas) {
    const float scale = screen_.uiScale();
    if (!dirty_ && canvas.width() == drawnW_ && canvas.height() == drawnH_ && scale == drawnScale_) return false;
    drawnW_ = canvas.width();
    drawnH_ = canvas.height();
    drawnScale_ = scale;
    dirty_ = false;
    const float w = static_cast<float>(drawnW_), h = static_cast<float>(drawnH_);
    // Measure; too tall for the window (a small one): the whole layout smaller,
    // to fit. Then centre the header and the cards a little in the room above
    // the footer.
    layoutScale_ = scale;
    float bottom = paint(nullptr, w, h, 0);
    for (int i = 0; i < 3 && neededH_ > h + 0.5f && layoutScale_ > scale * 0.6f; ++i) {
        layoutScale_ = std::max(scale * 0.6f, layoutScale_ * h / neededH_);
        bottom = paint(nullptr, w, h, 0);
    }
    const float room = footerTop_ - 56 * layoutScale_ - bottom;
    paint(&canvas, w, h, std::round(std::max(0.f, room) * 0.4f));
    return true;
}

float SetupScene::paint(Canvas* cv, float W, float H, float top) {
    const float s = layoutScale_ > 0 ? layoutScale_ : 1;
    const UiFont& R = *regular_;
    const UiFont& B = *bold_;
    auto pt = [s](float v) { return v * s; };
    auto text = [&](const UiFont& f, float x, float y, float lh, float px, std::string_view t, Color c) {
        if (cv && !t.empty()) f.draw(*cv, std::round(x), baselineIn(f, y, lh, px), t, px, c);
    };
    const bool narrow = W < pt(600);
    const float margin = pt(narrow ? kNarrowMargin : kMargin);
    const float contentW = std::min(W - 2 * margin, pt(kMaxContent));
    const float x0 = std::round((W - contentW) / 2);

    // The background.
    if (cv) {
        cv->resetClip();
        cv->verticalGradient(color::kBgTop, color::kBgBottom);
        cv->radialGlow(W * 0.18f, -H * 0.1f, std::max(W, H) * 0.75f, color::kGlow);
    }

    // A button: its rectangle, drawn.
    auto buttonWidth = [&](const Button& b, float px) {
        return std::max(pt(b.primary ? 120 : 84), std::round(B.measure(b.label, px) + 2 * pt(kButtonPad)));
    };
    auto drawButton = [&](std::size_t i, RectF r, float px) {
        Button& b = buttons_[i];
        b.rect = r;
        if (!cv) return;
        const bool on = enabled(b.action);
        const bool hot = on && static_cast<int>(i) == hover_;
        const bool down = on && static_cast<int>(i) == pressed_;
        const float radius = pt(kButtonRadius);
        Color fill, ink;
        if (!on) {
            fill = color::kDisabled;
            ink = color::kDisabledText;
        } else if (b.primary) {
            fill = down ? color::kAccentDown : hot ? color::kAccentHover : color::kAccent;
            ink = Color::hex(0xFFFFFF);
        } else {
            fill = down ? color::kButtonDown : hot ? color::kButtonHover : color::kButton;
            ink = color::kText;
        }
        if (b.primary && on) cv->fillRoundRect({r.x, r.y + pt(2), r.w, r.h}, radius, Color::hex(0x000000, 60));
        cv->fillRoundRect(r, radius, fill);
        if (!b.primary || !on) cv->strokeRoundRect(r, radius, std::max(1.f, pt(1)), on ? color::kButtonBorder : color::kCardBorder);
        const float tw = B.measure(b.label, px);
        text(B, r.x + (r.w - tw) / 2, r.y, r.h, px, b.label, ink);
        if (focusVisible_ && on && static_cast<int>(i) == focus_)
            cv->strokeRoundRect(r.inset(-pt(3.5f)), radius + pt(3.5f), pt(2), color::kFocus);
    };

    // ---- The header.
    float y = margin + top + pt(narrow ? 4 : 12);
    {
        const float em = pt(46);
        if (cv) drawEmblem(*cv, x0, y, em);
        const float tx = x0 + em + pt(16);
        text(B, tx, y - pt(1), pt(30), pt(kTitlePx), "Covert Reaction", color::kText);
        text(R, tx, y + pt(28), pt(20), pt(kSubtitlePx), config_.inGame ? "Game files and ROMs" : "Set up the game files",
             color::kText2);
        y += em + pt(28);
    }

    // ---- A card: icon, title + badge, status lines, an optional progress bar,
    // a detail line, its buttons (right of the text, or under it if narrow).
    struct Card {
        Status status;
        std::string title, badge;
        std::vector<std::pair<std::string, Color>> lines;   // (wrapped)
        std::string detail;
        bool progress = false;
        std::vector<std::size_t> buttons;
        bool detailIsPath = true;   // cut in the middle to fit (else wrapped, as prose)
    };
    auto drawCard = [&](const Card& card) {
        const float pad = pt(kCardPad), icon = pt(kIcon);
        const float textX = x0 + pad + icon + pt(14);
        const float right = x0 + contentW - pad;
        float buttonsW = 0;
        for (std::size_t i : card.buttons) buttonsW += buttonWidth(buttons_[i], pt(kButtonPx)) + (buttonsW > 0 ? pt(kButtonGap) : 0);
        // The buttons go right of the text if both cards' buttons fit there, else under it.
        float widest = 0;
        {
            float game = 0, roms = 0;
            for (const Button& b : buttons_) {
                if (b.action == Action::Quit || b.action == Action::Start) continue;
                float& g = isRomAction(b.action) ? roms : game;
                g += buttonWidth(b, pt(kButtonPx)) + (g > 0 ? pt(kButtonGap) : 0);
            }
            widest = std::max(game, roms);
        }
        const bool side = right - widest - pt(24) - textX >= pt(300);
        const float textW = side ? right - widest - pt(24) - textX : right - textX;
        // Measure.
        const float titleLh = pt(24), bodyLh = pt(20), smallLh = pt(18);
        std::vector<std::pair<std::string, Color>> wrapped;
        for (const auto& [line, c] : card.lines)
            for (auto& l : wrapText(R, line, pt(kBodyPx), textW)) wrapped.emplace_back(std::move(l), c);
        float contentH = titleLh + pt(2) + bodyLh * static_cast<float>(wrapped.size());
        if (card.progress) contentH += pt(12) + pt(6) + pt(4);
        std::vector<std::string> detail;
        if (card.detail.empty()) {
        } else if (card.detailIsPath) {
            detail.push_back(elideMiddle(R, card.detail, pt(kSmallPx), textW));
        } else {
            detail = wrapText(R, card.detail, pt(kSmallPx), textW);
        }
        if (!detail.empty()) contentH += pt(2) + smallLh * static_cast<float>(detail.size());
        // The buttons when they go under the text: rows that fit.
        std::vector<RectF> rects;
        float bx = textX, by = 0, rowsH = 0;
        if (!side) {
            by = contentH + pt(14);
            for (std::size_t i : card.buttons) {
                const float bw = buttonWidth(buttons_[i], pt(kButtonPx));
                if (bx > textX && bx + bw > right) {
                    bx = textX;
                    by += pt(kButtonH) + pt(kButtonGap);
                }
                rects.push_back({bx, by, bw, pt(kButtonH)});
                bx += bw + pt(kButtonGap);
            }
            rowsH = by + pt(kButtonH) - contentH;
        }
        const float cardH = std::round(std::max({contentH + rowsH + 2 * pad, icon + 2 * pad, pt(kButtonH) + 2 * pad}));
        const RectF box{x0, std::round(y), contentW, cardH};
        if (cv) {
            cv->fillRoundRect({box.x, box.y + pt(3), box.w, box.h}, pt(kCardRadius), Color::hex(0x000000, 50));
            cv->fillRoundRect(box, pt(kCardRadius), color::kCard);
            cv->strokeRoundRect(box, pt(kCardRadius), std::max(1.f, pt(1)), color::kCardBorder);
            drawIcon(*cv, card.status, x0 + pad, box.y + pad + (titleLh - icon) / 2 + pt(1), icon,
                     static_cast<float>(ticks_) * 0.13f);
        }
        float ty = box.y + pad;
        text(B, textX, ty, titleLh, pt(kCardTitlePx), card.title, color::kText);
        if (!card.badge.empty() && cv) {
            const float bpx = pt(kBadgePx);
            const float bw = std::round(B.measure(card.badge, bpx) + pt(14)), bh = pt(19);
            const float bxx = std::round(textX + B.measure(card.title, pt(kCardTitlePx)) + pt(10));
            const float byy = std::round(ty + (titleLh - bh) / 2);
            const bool required = card.badge == "Required";
            cv->fillRoundRect({bxx, byy, bw, bh}, bh / 2, required ? color::kAccent.withAlpha(46) : Color::hex(0x2A3242));
            text(B, bxx + pt(7), byy, bh, bpx, card.badge, required ? color::kAccentHover : color::kText2);
        }
        ty += titleLh + pt(2);
        for (const auto& [line, c] : wrapped) {
            text(R, textX, ty, bodyLh, pt(kBodyPx), line, c);
            ty += bodyLh;
        }
        if (card.progress && job_) {
            ty += pt(12);
            const float frac = static_cast<float>(std::clamp(job_->progress(), 0.0, 1.0));
            if (cv) {
                cv->fillRoundRect({textX, ty, textW, pt(6)}, pt(3), color::kTrack);
                cv->fillRoundRect({textX, ty, std::max(pt(6), textW * frac), pt(6)}, pt(3), color::kAccent);
            }
            ty += pt(6) + pt(4);
        }
        if (!detail.empty()) ty += pt(2);
        for (const std::string& l : detail) {
            text(R, textX, ty, smallLh, pt(kSmallPx), l, color::kText3);
            ty += smallLh;
        }
        // The buttons.
        if (side) {
            float x = right - buttonsW;
            const float yy = std::round(box.y + (cardH - pt(kButtonH)) / 2);
            for (std::size_t i : card.buttons) {
                const float bw = buttonWidth(buttons_[i], pt(kButtonPx));
                drawButton(i, {std::round(x), yy, bw, pt(kButtonH)}, pt(kButtonPx));
                x += bw + pt(kButtonGap);
            }
        } else {
            for (std::size_t k = 0; k < card.buttons.size(); ++k) {
                RectF r = rects[k];
                r.x = std::round(r.x);
                r.y = std::round(box.y + pad + r.y);
                drawButton(card.buttons[k], r, pt(kButtonPx));
            }
        }
        y = box.bottom() + pt(kCardGap);
    };

    // ---- The game files.
    {
        Card card{Status::Empty, "Game files", "Required", {}, {}, false, {}};
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            const Action a = buttons_[i].action;
            if (a == Action::ChooseGame || a == Action::ChooseGameFile || a == Action::ChooseGameFolder) card.buttons.push_back(i);
        }
        if (job_) {
            card.status = Status::Busy;
            card.lines.emplace_back("Importing from " + fileName(importFrom_) + "…", color::kText);
            card.progress = true;
            const int pct = static_cast<int>(std::lround(job_->progress() * 100));
            std::string stage = job_->stage();
            card.detail = (stage.empty() ? std::string("Starting") : stage) + " · " + std::to_string(pct) + "%";
            card.detailIsPath = false;
        } else if (!importError_.empty()) {
            card.status = Status::Error;
            card.lines.emplace_back(sentence(importError_), color::kRed);
            card.detail = displayPath(importFrom_);
            if (gameOk_) card.lines.emplace_back("The game files imported before are still there.", color::kText2);
        } else if (imported_ && gameOk_) {
            card.status = Status::Ok;
            card.lines.emplace_back(std::string("Imported from ") + describeGameSource(lastImport_.source) + " · " +
                                        std::to_string(lastImport_.files) + " files",
                                    color::kText);
            card.detail = displayPath(importFrom_);
            if (!gameCheck_.ok()) {
                card.status = Status::Warning;
                card.lines.emplace_back(gameCheck_.describe(), color::kAmber);
            }
        } else if (game_.empty()) {
            card.status = Status::Empty;
            card.lines.emplace_back("Not chosen", color::kText2);
            card.detail = "Choose your copy of Sid Meier’s Covert Action (1990).";
            card.detailIsPath = false;
        } else if (!isDir(game_)) {
            card.status = Status::Error;
            card.lines.emplace_back("That folder is gone.", color::kRed);
            card.detail = displayPath(game_);
        } else if (!gameOk_) {
            card.status = Status::Error;
            std::string missing = "Not Covert Action’s files: missing";
            for (std::size_t i = 0; i < gameMissing_.size(); ++i) missing += (i ? ", " : " ") + gameMissing_[i];
            card.lines.emplace_back(missing, color::kRed);
            card.detail = displayPath(game_);
        } else {
            card.status = gameCheck_.ok() ? Status::Ok : Status::Warning;
            card.lines.emplace_back("Covert Action’s files are ready", color::kText);
            if (!gameCheck_.ok()) card.lines.emplace_back(gameCheck_.describe(), color::kAmber);
            card.detail = displayPath(game_);
        }
        drawCard(card);
    }

    // ---- The MT-32 ROMs.
    {
        Card card{Status::Empty, "MT-32 ROMs", "Optional", {}, {}, false, {}};
        for (std::size_t i = 0; i < buttons_.size(); ++i)
            if (isRomAction(buttons_[i].action)) card.buttons.push_back(i);
        if (!romError_.empty()) {
            card.status = Status::Warning;
            card.lines.emplace_back("No MT-32 ROMs taken from " + fileName(romFrom_) + ": " + romError_ + ".",
                                    color::kAmber);
            if (romSet_) card.lines.emplace_back("Still using " + romSet_->describe(), color::kText2);
            card.detail = displayPath(romFrom_);
        } else if (!roms_.empty()) {
            card.detail = displayPath(roms_);
            if (romSet_) {
                card.status = Status::Ok;
                card.lines.emplace_back(romSet_->describe(), color::kText);
                if (!romFrom_.empty()) card.detail = "Copied from " + displayPath(romFrom_);
            } else if (!isDir(roms_)) {
                card.status = Status::Warning;
                card.lines.emplace_back("That folder is gone: the music plays on the AdLib.", color::kAmber);
            } else {
                card.status = Status::Warning;
                card.lines.emplace_back("No usable MT-32 control + PCM ROM pair in this folder: the music plays on the AdLib.",
                                        color::kAmber);
            }
        } else if (romSet_) {
            card.status = Status::Ok;
            card.lines.emplace_back(romSet_->describe(), color::kText);
            card.detail = "Found in " + displayPath(romFoundIn_);
        } else {
            card.status = Status::Empty;
            card.lines.emplace_back("Not needed: the music plays on the AdLib", color::kText2);
            card.detail = "With a Roland MT-32’s control and PCM ROMs, the music plays as it was written.";
            card.detailIsPath = false;
        }
        drawCard(card);
    }
    const float cardsBottom = y - pt(kCardGap);

    // ---- The footer: a note and the help on the left, Quit and START on the right.
    {
        const std::size_t quitI = static_cast<std::size_t>(indexOf(Action::Quit));
        const std::size_t startI = static_cast<std::size_t>(indexOf(Action::Start));
        const float fpx = pt(kButtonPx + 0.5f);
        const float qw = buttonWidth(buttons_[quitI], fpx), sw = buttonWidth(buttons_[startI], fpx);
        const float fbW = qw + pt(kButtonGap) + sw, fbH = pt(kFooterButtonH);
        const bool stack = contentW - fbW - pt(32) < pt(280);
        const float helpW = stack ? contentW : contentW - fbW - pt(32);
        const float lh = pt(18);
        std::vector<std::pair<std::string, Color>> lines;
        if (config_.inGame && gameChanged())
            for (auto& l : wrapText(R, "The new game files are used from the next start.", pt(kSmallPx), helpW))
                lines.emplace_back(std::move(l), color::kAmber);
        for (auto& l : wrapText(R, kHelp, pt(kSmallPx), helpW)) lines.emplace_back(std::move(l), color::kText3);
        const float helpH = lh * static_cast<float>(lines.size());
        const float footerH = stack ? helpH + pt(16) + fbH : std::max(helpH, fbH);
        footerTop_ = H - margin - footerH;
        neededH_ = cardsBottom + pt(24) + footerH + margin;
        const float footerTop = std::max(footerTop_, cardsBottom + pt(24));
        // A hairline above the footer.
        if (cv) cv->fillRect({x0, std::round(footerTop - pt(16)), contentW, std::max(1.f, pt(1))}, color::kCardBorder.withAlpha(160));
        float hy = stack ? footerTop : footerTop + std::max(0.f, (fbH - helpH) / 2);
        for (const auto& [l, c] : lines) {
            text(R, x0, hy, lh, pt(kSmallPx), l, c);
            hy += lh;
        }
        const float by = std::round(stack ? footerTop + helpH + pt(16) : footerTop + (footerH - fbH) / 2);
        const float right = x0 + contentW;
        drawButton(startI, {std::round(right - sw), by, sw, fbH}, fpx);
        drawButton(quitI, {std::round(right - sw - pt(kButtonGap) - qw), by, qw, fbH}, fpx);
    }
    return cardsBottom;
}

}  // namespace reaction
