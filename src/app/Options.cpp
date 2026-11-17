#include "app/Options.h"

#include <array>
#include <filesystem>

#include "app/SetupScene.h"
#include "core/Folders.h"
#include "formats/McgaPalette.h"

namespace reaction {

namespace {

constexpr const char* kSounds[] = {"auto", "mt32", "adlib", "speaker", "none"};
constexpr const char* kSoundNames[] = {"Automatic", "Roland MT-32", "AdLib", "PC speaker", "Off"};
#ifdef __EMSCRIPTEN__
constexpr int kDone = 6;   // (the page has its own full-screen button and file picker)
#else
constexpr int kSetup = 7;
constexpr int kDone = 8;
#endif

// `f` in the game's palette, at half brightness: each of its colours becomes the
// game palette's nearest to it.
Frame dimmed(const Frame& f, const Palette& game) {
    std::array<std::uint8_t, 256> map{};
    for (int i = 0; i < 256; ++i) {
        const Rgb& c = f.palette.colors[static_cast<std::size_t>(i)];
        const int r = c.r / 2, g = c.g / 2, b = c.b / 2;
        int best = 0, bestD = 1 << 30;
        for (int j = 0; j < 256; ++j) {
            const Rgb& k = game.colors[static_cast<std::size_t>(j)];
            const int dr = k.r - r, dg = k.g - g, db = k.b - b;
            const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bestD) bestD = d, best = j;
        }
        map[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(best);
    }
    Frame out;
    out.palette = game;
    out.image = f.image;
    for (int y = 0; y < out.image.height(); ++y)
        for (int x = 0; x < out.image.width(); ++x) out.image.set(x, y, map[out.image.at(x, y)]);
    return out;
}

}  // namespace

bool OptionsScene::isOptionsKey(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN || e.key.repeat) return false;
    if (e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) return false;
    return e.key.keysym.sym == SDLK_BACKQUOTE || e.key.keysym.scancode == SDL_SCANCODE_GRAVE;
}

OptionsScene::OptionsScene(App& app, Frame background, Done done, bool overlay, int selected, bool dim)
    : app_(app), done_(std::move(done)), overlay_(overlay) {
    const Palette game = mcgaPalette(palettes::kGame);
    if (overlay && dim) {
        background_ = dimmed(background, game);
    } else {
        background_ = std::move(background);
        background_.palette = game;
    }
    open(selected);
}

void OptionsScene::handle(const SDL_Event& e) {
    if (overlay_ && isOptionsKey(e)) {
        close();
        return;
    }
    if (menu_) menu_->handle(e, app_.mouse(e));
}

void OptionsScene::tick() {
    if (!menu_ || closed_ || next_) return;
    menu_->tick();
    auto r = menu_->result();
    if (!r) return;
    if (*r < 0 || *r == kDone) {
        close();
        return;
    }
#ifndef __EMSCRIPTEN__
    if (*r == kSetup) {
        openSetup();
        return;
    }
#endif
    change(*r);
    open(*r);
}

void OptionsScene::draw(Frame& f) {
    f.palette = background_.palette;
    f.image = background_.image;
    if (menu_ && !closed_) menu_->draw(f.image);
}

void OptionsScene::close() {
    if (done_) next_ = done_();
    if (!next_) closed_ = true;
    menu_.reset();
}

void OptionsScene::open(int selected) {
    const Settings& s = app_.settings;
    auto onOff = [](bool b) { return b ? "On" : "Off"; };
    std::string text = "Options\n";
    text += std::string(" Keypad on 789/UIO/JKL: ") + onOff(s.keypadKeys) + "\n";
    text += std::string(" Copy protection: ") + onOff(s.copyProtection) + "\n";
    text += std::string(" Sound: ") + kSoundNames[soundIndex()] + "\n";
    text += std::string(" Animations: ") + onOff(s.animations) + "\n";
    text += std::string(" MT-32 clean mixing: ") + onOff(s.mt32CleanMixing) + "\n";
    text += std::string(" Pause after break-in: ") + onOff(s.tacEndPause) + "\n";
#ifndef __EMSCRIPTEN__
    text += std::string(" Full screen: ") + onOff(app_.screen.fullscreen()) + "\n";
    text += " Game files and ROMs...\n";
#endif
    text += " Done\n";
    TextWindowOptions opt;
    opt.selected = selected;
    menu_.emplace(app_.fonts.font(1), std::move(text), 64, 80, opt).setHotkeys(appHotkeys(app_, *this));
}

int OptionsScene::soundIndex() const {
    for (int i = 0; i < 5; ++i)
        if (app_.settings.sound == kSounds[i]) return i;
    return 0;
}

void OptionsScene::change(int item) {
    Settings& s = app_.settings;
    switch (item) {
    case 0: s.keypadKeys = !s.keypadKeys; break;
    case 1: s.copyProtection = !s.copyProtection; break;
    case 2: s.sound = kSounds[(soundIndex() + 1) % 5]; break;   // (main.cpp switches the device)
    case 3:                                                     // options bit 8 (App::syncAnimations)
        s.animations = !s.animations;
        app_.syncAnimations();
        break;
    case 4: s.mt32CleanMixing = !s.mt32CleanMixing; break;      // (main.cpp passes it to the engine)
    case 5: s.tacEndPause = !s.tacEndPause; break;              // (TacScene passes it to TAC)
#ifndef __EMSCRIPTEN__
    case 6:
        app_.screen.toggleFullscreen();
        s.fullscreen = app_.screen.fullscreen();
        break;
#endif
    default: return;
    }
    app_.saveSettings();
}

void OptionsScene::openSetup() {
#ifndef __EMSCRIPTEN__
    // Done saves both folders; the game folder is used from the next start, the
    // ROM folder at once (main.cpp gives the sound engine the new places to look
    // when settings.mt32Roms changes). Then this window again.
    SetupConfig c = app_.setup;
    c.gameDir = app_.settings.gameData.empty() ? app_.data.dir() : folders::fromUtf8(app_.settings.gameData);
    c.romDir = app_.settings.mt32Roms.empty() ? std::filesystem::path() : folders::fromUtf8(app_.settings.mt32Roms);
    c.inGame = true;
    App& app = app_;
    Frame bg = background_;   // (already dimmed / in the game's palette)
    Done done = done_;
    const bool overlay = overlay_;
    next_ = std::make_unique<SetupScene>(app_.screen, std::move(c), [&app, bg, done, overlay](bool ok, const SetupScene& s) {
        if (ok) {
            if (s.gameChanged()) app.settings.gameData = folders::toUtf8(s.gameDir());
            app.settings.mt32Roms = s.romDir().empty() ? std::string() : folders::toUtf8(s.romDir());
            app.saveSettings();
        }
        return std::unique_ptr<Scene>(std::make_unique<OptionsScene>(app, bg, done, overlay, kSetup, false));
    });
    menu_.reset();
#endif
}

}  // namespace reaction
