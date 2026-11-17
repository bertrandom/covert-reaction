#include "app/Options.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>
#include <filesystem>

#include "app/SetupScene.h"
#include "core/Folders.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"

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

// What each item does, shown under the window while it's highlighted.
const char* const kHelp[] = {
    "7 8 9, U I O and J K L work as the numeric keypad in break-ins and wiretaps, "
    "for keyboards without one.",
    "The leader line-up after a briefing counts: a wrong face means a practice "
    "mission, as in the 1990 release. GOG's release has no check.",
    "What plays the music and sounds. Automatic: the MT-32 if its ROMs are found, "
    "else the AdLib.",
    "The animations behind the briefings and the city's windows. Alt-A switches "
    "them in the game too.",
    "Mixes the MT-32's sounds in phase. The real MT-32 can cancel some out, such "
    "as a gunshot while music plays. Off is the original sound.",
    "After a break-in's last message, the original waits about 2 seconds. Off "
    "goes straight back.",
#ifndef __EMSCRIPTEN__
    "Fills the whole screen. Alt+Enter switches it too.",
    "Choose the game's files or your MT-32 ROMs again.",
#endif
    "Close the Options.",
};

// `text` wrapped into lines at most `width` pixels wide (at spaces).
std::string wrap(const Font& font, std::string_view text, int width) {
    std::string out, line;
    auto widthOf = [&](const std::string& s) {
        int w = 0;
        for (char ch : s) w += font.charWidth(static_cast<std::uint8_t>(ch));
        return w;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = text.find(' ', i);
        if (j == std::string_view::npos) j = text.size();
        const std::string word(text.substr(i, j - i));
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && widthOf(tryLine) > width) {
            out += line + "\n";
            line = word;
        } else {
            line = tryLine;
        }
        i = j + 1;
    }
    if (!line.empty()) out += line + "\n";
    return out;
}

// The keypad option's picture: the keys it turns into a keypad, cut out of a
// keyboard (drawn here, no image), each with the keypad digit it sends on a
// yellow badge; the neighbouring keys are dimmed. kKeysW x kKeysH pixels.
constexpr int kKeysW = 108, kKeysH = 49;

Image keypadKeys(const FontSet& fonts) {
    Image img(kKeysW, kKeysH, 0);
    constexpr int kKeyW = 21, kKeyH = 15, kStep = 23, kRowStep = 17;
    // A keyboard's three rows, each shifted right of the one above.
    struct Row {
        int shift;
        const char* keys;   // the row's keys from the one left of the keypad's
    };
    constexpr Row rows[] = {{0, "67890"}, {11, "YUIOP"}, {17, "HJKL;"}};
    const char* const digits[] = {"789", "456", "123"};
    // The keypad's keys (columns 1-3) centred.
    const int left = (kKeysW - (rows[2].shift + 3 * kStep + kKeyW - kStep)) / 2 - kStep;
    const Font& letters = fonts.font(1);
    const Font& small = fonts.font(2);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 5; ++c) {
            const int x = left + rows[r].shift + c * kStep, y = 1 + r * kRowStep;
            const bool keypad = c >= 1 && c <= 3;
            fillRect(img, x, y, kKeyW, kKeyH, keypad ? 8 : 0);
            drawRectOutline(img, x, y, kKeyW - 1, kKeyH - 1, keypad ? 7 : 8);
            const char label[2] = {rows[r].keys[c], 0};
            letters.draw(img, x + 3, y + 3, label, {0, static_cast<std::uint8_t>(keypad ? 15 : 8), 0});
            if (!keypad) continue;
            fillRect(img, x + 13, y + 5, 6, 8, 14);   // the badge
            const char digit[2] = {digits[r][c - 1], 0};
            small.draw(img, x + 14, y + 6, digit, {0, 0, 14});
        }
    }
    return img;
}

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
    if (menu_) menu_->handle(e);
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
    if (!menu_ || closed_) return;
    menu_->draw(f.image);
    // The highlighted item's description, in a box of its own under the window.
    const int item = menu_->selection();
    if (item < 0 || item >= static_cast<int>(std::size(kHelp))) return;
    const Font& font = app_.fonts.font(1);
    constexpr int kX = 16, kW = 320 - 2 * kX;   // the box: 16 pixels from each side
    const int y = menu_->layout().boxY2 + 6;
    const bool keys = item == 0;   // the keypad option: its keys beside the text
    std::string text = wrap(font, kHelp[item], kW - 8 - (keys ? kKeysW + 6 : 0));
    // The same height for every item (the keys' height), so the box doesn't jump.
    for (int lines = static_cast<int>(std::count(text.begin(), text.end(), '\n')); lines < 6; ++lines) text += "\n";
    TextWindowOptions opt;
    opt.noSelect = true;
    const TextWindowLayout lay = layoutTextWindow(font, text, kX, y);
    // (drawTextWindow sizes the box to the text: widen it to kW.)
    drawBevelBox(f.image, kX, y, kW, lay.boxY2 - y, textWindowColors(y, opt).box);
    drawWrappedText(f.image, font, text, 64, kX + 5, y + 5, textWindowColors(y, opt).text);
    if (keys) {
        const Image k = keypadKeys(app_.fonts);
        f.image.blit(k, 0, 0, kKeysW, kKeysH, kX + kW - kKeysW - 3, y + 3);
    }
}

void OptionsScene::close() {
    if (done_) next_ = done_();
    if (!next_) closed_ = true;
    menu_.reset();
}

void OptionsScene::open(int selected) {
    const Settings& s = app_.settings;
    auto onOff = [](bool b) { return b ? "On" : "Off"; };
    std::string text = "Options\n\n";   // (a blank line under the title)
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
    // Centred, near the top: the description box goes under it.
    const TextWindowLayout lay = layoutTextWindow(app_.fonts.font(1), text, 0, 0);
    const int x = (320 - (lay.maxWidth + 8)) / 2;
    menu_.emplace(app_.fonts.font(1), std::move(text), x, 24, opt).setHotkeys(appHotkeys(app_, *this));
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
