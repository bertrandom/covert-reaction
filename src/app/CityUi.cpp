#include "app/CityUi.h"

#include <algorithm>
#include <cstdio>

#include "app/DosKey.h"
#include "formats/Catalog.h"
#include "formats/McgaPalette.h"
#include "formats/Pic.h"
#include "formats/TextWindow.h"
#include "game/Paper.h"

namespace redux {

namespace {

constexpr double kTick = 1.0 / kTicksPerSecond;

// TELETYPE.C teletype_shapes: type, x1, y1, x2, y2, color.
struct Shape {
    int type, x1, y1, x2, y2, color;
};
constexpr Shape kTeletypeShapes[] = {
    {1, 0, 54, 319, 55, 8},      {1, 0, 56, 319, 199, 7},     {0, 0, 169, 319, 169, 8},
    {0, 0, 56, 0, 56, 7},        {0, 0, 57, 0, 167, 15},      {0, 0, 168, 0, 168, 7},
    {0, 1, 56, 1, 168, 8},       {0, 1, 169, 1, 169, 5},      {2, 8, 0, 40, 5, 0},
    {1, 31, 98, 290, 165, 7},    {0, 31, 97, 290, 97, 5},     {1, 30, 166, 291, 168, 5},
    {0, 31, 113, 31, 166, 8},    {0, 291, 97, 291, 167, 8},   {0, 290, 113, 290, 166, 8},
    {0, 30, 97, 30, 167, 8},     {1, 33, 160, 288, 165, 8},   {1, 50, 163, 269, 166, 7},
    {0, 270, 166, 270, 166, 8},  {0, 50, 167, 270, 167, 8},   {0, 270, 166, 270, 166, 8},
    {0, 50, 167, 270, 167, 8},   {0, 30, 169, 291, 169, 7},   {0, 0, 177, 319, 177, 15},
    {1, 4, 184, 124, 195, 5},    {1, 123, 185, 124, 194, 8},  {1, 6, 185, 122, 194, 2},
    {0, 4, 196, 124, 196, 15},   {1, 146, 183, 178, 196, 5},  {1, 148, 184, 176, 194, 7},
    {0, 147, 184, 147, 194, 15}, {0, 148, 185, 148, 193, 15}, {0, 149, 185, 174, 185, 15},
    {0, 176, 185, 176, 193, 8},  {0, 177, 184, 177, 194, 8},  {2, 41, 0, 3, 7, 0},
    {0, 270, 184, 319, 184, 5},  {1, 270, 185, 319, 199, 8},  {1, 272, 197, 319, 199, 7},
    {1, 274, 189, 295, 193, 5},  {1, 274, 188, 294, 192, 3},  {0, 273, 188, 273, 192, 11},
    {0, 274, 188, 293, 188, 11}, {1, 303, 189, 307, 193, 5},  {1, 303, 188, 306, 192, 4},
    {0, 302, 188, 302, 192, 12}, {0, 303, 188, 305, 188, 12}, {2, 9, 0, 2, 4, 0},
    {-1, 0, 0, 0, 0, 0},
};

// draw_shape_list (type 2 repeats the previous y2 + 1 shapes x2 - 1 more times,
// offset by (x1, y1) each time).
void drawShapeList(Image& img) {
    std::array<Shape, std::size(kTeletypeShapes)> list{};
    std::copy(std::begin(kTeletypeShapes), std::end(kTeletypeShapes), list.begin());
    int idx = 0, ox = 0, oy = 0, times = 0;
    do {
        Shape& s = list[static_cast<std::size_t>(idx)];
        const auto c = static_cast<std::uint8_t>(s.color);
        switch (s.type) {
        case 0: drawLine(img, s.x1 + ox, s.y1 + oy, s.x2 + ox, s.y2 + oy, c); break;
        case 1: fillRectXY(img, s.x1 + ox, s.y1 + oy, s.x2 + ox, s.y2 + oy, c); break;
        case 2:
            if (s.color == 0) {
                ox = oy = 0;
                times = s.x2;
                s.color = 1;
            }
            if (--times) {
                ox += s.x1;
                oy += s.y1;
                idx -= s.y2 + 1;
            } else {
                s.color = 0;
                ox = oy = 0;
            }
            break;
        case -1: idx = -1; break;
        default: break;
        }
    } while (++idx);
}

std::string stripSpaces(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}

// palette2 (TEXTWIN.C): colour 12 shows as 6, for the darkest skin tone.
constexpr PaletteMap kPalette2 = {0, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11, 6, 13, 14, 15, 0};

}  // namespace

CityUi::CityUi(App& app, GameThread& thread)
    : app_(app), thread_(thread), faces_(FaceSprites::load(app.data)), palette_(mcgaPalette(palettes::kGame)) {}

// ---------------------------------------------------------------- requests

void CityUi::ask(Request r) {
    req_ = r;
    thread_.yield();
}

int CityUi::textWindow(const std::string& text, int x, int y, unsigned flags) {
    const bool paused = flags & SimEvent::kNoWait;
    const bool menuFlag = flags & SimEvent::kNoSelect;
    const bool animActive = pending_.has_value();
    if (animActive && !paused) {
        back = screen;
        PanOptions o;
        o.x = pending_->x;
        o.y = pending_->y;
        anim_ = std::make_unique<PanPlayer>(PanFile::parse(app_.data.read(pending_->name)), o, &screen);
        for (int i = 0; i < PanPlayer::kVars; ++i) anim_->setVar(i, vars_[static_cast<std::size_t>(i)]);
        cityPicShown_ = -1;
        bldFlag_ = pending_->name.compare(0, 3, "bld") == 0;
        palette_ = mcgaPalette(palettes::kGame);
        const int portrait = sim_ ? sim_->globals().faceShown : -1;
        if (portrait != -1) {
            if (((portrait >> 2) & 3) == 3) palette_ = mcgaPalette(kPalette2);
            redux::drawFaceParts(anim_->backPage(), faces_, portrait, portraitX, portraitY);
        }
        anim_->step();
        anim_->step();
    }
    if (sim_) sim_->globals().faceShown = -1;   // portrait = -1
    const TextWindowLayout lay = layoutTextWindow(font(), text, x, y);
    if (y & 1) {
        if (animActive) {
            screenW = clampTo(animX, lay.boxX2 + 5, 999);
            fillColor = 5;
            if (y < 144) {
                fill(0, 0, screenW, 200, 5);
                if (sim_) sim_->drawStatusBar();
            } else {
                fill(4, y, 312, 198 - y, 5);
            }
        }
    } else {
        fillColor = 5;
    }
    TextWindowOptions opt;
    opt.fillColor = static_cast<std::uint8_t>(fillColor);
    opt.noSelect = menuFlag;
    opt.selected = 0;
    if (paused) {
        TextWindowOptions o2 = opt;
        o2.selected = -1;
        drawTextWindow(screen, font(), text, x, y, o2);
        return -1;
    }
    menu_.emplace(font(), text, x, y, opt);
    animSw_ = std::min(screenW, 320);
    keys_.clear();   // flush_input
    Request r;
    r.kind = Request::Menu;
    ask(r);
    pending_.reset();
    return req_.result;
}

void CityUi::waitKey() {
    Request r;
    r.kind = Request::Key;
    r.flushAfter = true;
    ask(r);
}

int CityUi::getKey() {
    Request r;
    r.kind = Request::Key;
    ask(r);
    return req_.result;
}

bool CityUi::keyPending() const { return !keys_.empty(); }

void CityUi::flushInput() { keys_.clear(); }

void CityUi::wait(int retraces) {
    if (retraces <= 0) return;
    Request r;
    r.kind = Request::Delay;
    r.ticks = retraces;
    ask(r);
}

void CityUi::playAnimation(const std::string& name, int x) {
    if (!animations || !app_.data.exists(name)) {
        fill(x, 0, 320 - x, 200, 5);
        return;
    }
    back = screen;
    PanOptions o;
    o.x = x;
    anim_ = std::make_unique<PanPlayer>(PanFile::parse(app_.data.read(name)), o, &screen);
    playing_ = true;
    bldFlag_ = false;
    animSw_ = 0;
    Request r;
    r.kind = Request::Delay;
    r.ticks = -1;   // until the animation ends (or a key)
    ask(r);
    playing_ = false;
    anim_.reset();
    cityPicShown_ = -1;
    palette_ = mcgaPalette(palettes::kGame);
}

namespace {
// The scene runScene's child moves on to when it is done.
class ChildDone : public Scene {
public:
    explicit ChildDone(const Frame& f) : last(f) {}
    void draw(Frame& f) override { f = last; }
    Frame last;
};
}  // namespace

Frame CityUi::runScene(const SceneMaker& make) {
    Frame f;
    f.image = composite();
    f.palette = palette_;
    child_ = make(f, [](const Frame& last) -> std::unique_ptr<Scene> { return std::make_unique<ChildDone>(last); });
    childLast_ = f;
    keys_.clear();
    Request r;
    r.kind = Request::Child;
    ask(r);
    keys_.clear();
    return childLast_;
}

// ---------------------------------------------------------------- scene side

void CityUi::handle(const SDL_Event& e) {
    if (req_.kind == Request::Child && child_) {
        child_->handle(e);
        return;
    }
    if (req_.kind == Request::Menu && menu_) {
        if (e.type == SDL_KEYDOWN) keys_.push_back(e);     // (a key stops the animation: see tick)
        menu_->handle(e, app_.mouse(e));
        return;
    }
    if (e.type == SDL_KEYDOWN && dosKey(e) >= 0) keys_.push_back(e);
    else if (e.type == SDL_MOUSEBUTTONDOWN) {
        SDL_Event k{};
        k.type = SDL_KEYDOWN;
        k.key.keysym.sym = SDLK_RETURN;
        keys_.push_back(k);
    }
}

bool CityUi::tick() {
    switch (req_.kind) {
    case Request::None:
        return false;
    case Request::Delay:
        if (playing_) {
            if (anim_ && !anim_->finished() && !(keys_.size() && anim_->framesShown() >= 3)) {
                anim_->advance(kTick);
                if (!anim_->finished()) return false;
            }
            keys_.clear();
            req_.kind = Request::None;
            return true;
        }
        if (--req_.ticks > 0) return false;
        req_.kind = Request::None;
        return true;
    case Request::Key:
        if (keys_.empty()) return false;
        req_.result = dosKey(keys_.front());
        keys_.pop_front();
        if (req_.flushAfter) keys_.clear();
        req_.kind = Request::None;
        return true;
    case Request::Child: {
        if (!child_) return false;
        child_->tick();
        if (child_->quit) {
            childQuit_ = true;
            return false;
        }
        auto n = child_->next();
        if (!n) return false;
        if (auto* done = dynamic_cast<ChildDone*>(n.get())) {
            childLast_ = done->last;
            child_.reset();
            req_.kind = Request::None;
            return true;
        }
        child_ = std::move(n);
        return false;
    }
    case Request::Menu:
        // A key stops the animation (anim_should_abort); it keeps its frame.
        if (anim_ && !anim_->finished() && (keys_.empty() || anim_->framesShown() < 3)) anim_->advance(kTick);
        menu_->tick();
        if (auto r = menu_->result()) {
            req_.result = *r;
            Image last = composite();
            menu_->draw(last);
            screen = last;
            menu_.reset();
            anim_.reset();
            keys_.clear();
            req_.kind = Request::None;
            return true;
        }
        return false;
    }
    return false;
}

Image CityUi::composite() const {
    if (!anim_) return screen;
    Image img = anim_->frame();
    if (animSw_ > 0) img.blit(screen, 0, 0, animSw_, 200, 0, 0);
    if (bldFlag_) img.blit(screen, 0, 0, 160, 30, 0, 0);   // restore_background's status bar
    return img;
}

void CityUi::draw(Frame& f) const {
    if (req_.kind == Request::Child && child_) {
        child_->draw(f);
        return;
    }
    f.palette = palette_;
    f.image = composite();
    if (menu_) menu_->draw(f.image);
}

// ---------------------------------------------------------------- SimUi events

int CityUi::event(const SimEvent& e) {
    switch (e.kind) {
    case SimEvent::Kind::Teletype: incomingMessage(e.title, e.text); return -1;
    case SimEvent::Kind::Notice: return textWindow(e.text, e.x, e.y, e.flags);
    case SimEvent::Kind::Wait: waitKey(); return -1;
    case SimEvent::Kind::CaseOver: throw CaseOverExit{};
    default: return -1;
    }
}

// ---------------------------------------------------------------- drawing

void CityUi::fill(int x, int y, int w, int h, int color) {
    redux::fillRect(screen, x, y, w, h, static_cast<std::uint8_t>(color));
}

void CityUi::recolor(int x, int y, int w, int h, int from, int to) {
    recolorRect(screen, x, y, w, h, static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to));
}

void CityUi::drawLineC(int x1, int y1, int x2, int y2, int color) {
    drawLine(screen, x1, y1, x2, y2, static_cast<std::uint8_t>(color));
}

void CityUi::drawOutline(int x, int y, int w, int h, int color) {
    drawRectOutline(screen, x, y, w, h, static_cast<std::uint8_t>(color));
}

void CityUi::drawBevel(int x, int y, int w, int h, int color) {
    drawBevelBox(screen, x, y, w, h, static_cast<std::uint8_t>(color));
    fillColor = color;
}

void CityUi::clearScreenNow() {
    fillColor = 5;
    fill(0, 0, 320, 200, 5);
}

void CityUi::clearPlayAreaNow() {
    fill(0, 0, screenW, 200, 5);
    if (sim_) sim_->drawStatusBar();
    fillColor = 5;
}

void CityUi::drawText(const std::string& s, int x, int y, int color) {
    TextAttr a;
    a.textbg = 1;
    a.color = static_cast<std::uint8_t>(color);
    a.background = static_cast<std::uint8_t>(fillColor);
    font().draw(screen, x, y, s, a);
}

void CityUi::drawTextT(const std::string& s, int x, int y, int color) {
    font().draw(screen, x, y, s, {0, static_cast<std::uint8_t>(color), 0});
}

void CityUi::drawTextCentered(const std::string& s, int x, int y, int color) {
    redux::drawTextCentered(screen, font(), s, x, y, static_cast<std::uint8_t>(color));
}

int CityUi::drawWrapped(const std::string& s, int width, int x, int y, int color) {
    return drawWrappedText(screen, font(), s, width, x, y, static_cast<std::uint8_t>(color));
}

void CityUi::drawPortrait(int face, int x, int y) {
    redux::drawPortrait(screen, faces_, face, x, y);
    if (sim_) sim_->globals().faceShown = -1;
}

void CityUi::drawFace(int face, int x, int y) {
    redux::drawFaceParts(screen, faces_, face, x, y);
    if (sim_) sim_->globals().faceShown = -1;
}

void CityUi::drawSprite(int n, int x, int y) { faces_.draw(screen, n, x, y); }

void CityUi::cardPanel(int x, int y) { drawCardPanel(screen, x, y); }

void CityUi::drawPageFrame(const std::string& title, int color) {
    screenW = 320;
    redux::drawPageFrame(screen, font(), faces_, title, static_cast<std::uint8_t>(color));
    fillColor = 15;
}

void CityUi::drawTeletypePaper() {
    fill(0, 0, 320, 200, 15);
    drawLineC(8, 0, 8, 199, 7);
    drawLineC(0x137, 0, 0x137, 199, 7);
    for (int y = 4; y < 199; y += 12) {
        fill(2, y, 4, 4, 8);
        fill(0x13A, y, 4, 4, 8);
    }
    for (int y = 0x20; y < 199; y += 0x40) {
        drawLineC(0x17, y + 1, 0x17, y + 0x1D, 11);
        drawLineC(0x128, y + 1, 0x128, y + 0x1D, 11);
        for (int j = 0; j < 0x20; j += 2) drawLineC(0x18, y + j, 0x127, y + j, 11);
    }
}

void CityUi::drawStatusBar(const std::string& dateTime) {
    const int sy = statusY;
    fill(0, sy, 160, 26, 8);
    drawLineC(1, sy + 1, 1, sy + 24, 5);
    drawLineC(2, sy + 2, 2, sy + 24, 7);
    drawLineC(2, sy + 1, 158, sy + 1, 7);
    drawLineC(158, sy + 2, 158, sy + 24, 5);
    drawLineC(2, sy + 24, 158, sy + 24, 5);
    fill(5, sy + 3, 150, 9, 5);
    drawLineC(3, sy + 3, 5, sy + 3, 5);
    drawLineC(6, sy + 4, 6, sy + 11, 7);
    drawLineC(156, sy + 3, 156, sy + 11, 5);
    if (sim_) {
        const auto& w = sim_->world();
        const auto& c = w.cities[static_cast<std::size_t>(w.hdr.hereCity & 15)];
        drawTextCentered(c.name.str() + ", " + c.country.str(), 80, sy + 4, 10);
    }
    fill(15, sy + 14, 130, 9, 5);
    drawLineC(13, sy + 14, 15, sy + 14, 5);
    drawLineC(16, sy + 15, 16, sy + 22, 7);
    drawLineC(146, sy + 14, 146, sy + 22, 5);
    drawTextCentered(dateTime, 80, sy + 15, 10);
    statusY = 174;
}

void CityUi::loadPicture(const std::string& name, int page) {
    Image& dst = page == 0 ? screen : page == 1 ? back : work;
    if (!app_.data.exists(name)) return;
    auto pic = decodePicture(app_.data.read(name));
    dst.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
}

bool CityUi::setSceneBackground(const std::string& name, int x, int y) {
    if (!animations || !app_.data.exists(name)) {
        fill(0, 0, 320, 200, 5);
        animX = x;
        fillColor = 5;
        return false;
    }
    pending_ = SceneAnim{name, x, y};
    animX = x;
    return true;
}

void CityUi::setSceneVar(int i, int v) {
    if (i >= 0 && i < PanPlayer::kVars) vars_[static_cast<std::size_t>(i)] = v;
}

void CityUi::loadCityPicture(int c, int x, int y) {
    const auto& city = sim_->world().cities[static_cast<std::size_t>(c & 15)];
    if (c != cityPicShown_) {
        // show_city_from_catalog: the picture on the back page, measured by its
        // non-black first row and column.
        std::string name = stripSpaces(city.name.str());
        if (name.size() > 8) name.resize(8);
        name += ".pic";
        redux::fillRect(back, 0, 0, 320, 200, 0);
        cityPicW_ = cityPicH_ = 0;
        if (app_.data.exists("cities.cat")) {
            const auto cat = app_.data.read("cities.cat");
            if (auto off = catalogFind(cat, name)) {
                auto pic = decodePicture(cat, *off);
                back.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
            }
        }
        while (cityPicW_ < 160 && back.at(cityPicW_, 0)) ++cityPicW_;
        while (cityPicH_ < 100 && back.at(0, cityPicH_)) ++cityPicH_;
        work.blit(back, 0, 0, cityPicW_, cityPicH_, 0, 0);
        cityPicShown_ = c;
    }
    y -= cityPicH_ / 2;
    drawTextCentered(city.name.str() + ", " + city.country.str(), x, y - 10, 15);
    x -= cityPicW_ / 2;
    if (cityPicW_) {
        drawOutline(x - 1, y - 1, cityPicW_ + 1, cityPicH_ + 1, 15);
        screen.blit(work, 0, 0, cityPicW_, cityPicH_, x, y);
    }
    fillColor = 5;
}

// incoming_message: the teleprinter types the message line by line (a key
// skips to the end), then waits for a key and puts the screen back.
void CityUi::incomingMessage(const std::string& title, const std::string& text) {
    back = screen;
    fill(0, 0, 320, 100, 5);
    drawShapeList(screen);
    fill(0x32, 0x96, 0xDC, 0x0D, 15);
    drawTextCentered(title, 0xA0, 0x98, 12);
    drawTextCentered("Incoming Msg", 0x40, 0xBA, 10);
    drawLineC(0x31, 0x96, 0x31, 0xA2, 8);
    drawLineC(0x10E, 0x96, 0x10E, 0xA2, 8);
    work.blit(screen, 0, 100, 320, 100, 0, 100);
    redux::fillRect(work, 0x32, 0x44, 0xDC, 0x5F, 15);
    const int y = drawWrappedText(work, font(), text, 0x19, 0x3A, 0x4C, 5);
    flushInput();
    auto scroll = [&](int n) {
        // drv_copy_rect within the screen (overlapping: through a copy)
        const Image tmp = screen;
        screen.blit(tmp, 0x31, 0x96 - n * 8, 0xDE, n * 8 + 0x0D, 0x31, 0x8E - n * 8);
    };
    int n = 0, x = 0x34;
    for (n = 0; n < (y - 0x4C) / 8; ++n) {
        scroll(n);
        fill(0x32, 0x9B, 0xDC, 8, 15);
        app_.sound(0x1F);   // the teleprinter types
        for (x = 0x34; x < 0xF0; ++x) {
            screen.blit(work, x - 2, n * 8 + 0x4A, 2, 10, x - 2, 0x99);
            drawLineC(x - 1, 0xA3, x - 1, 0xA6, 7);
            drawSprite(0x6F, x, 0x9A);
            wait((x & 7) ? 1 : 3);
            if (keyPending()) break;
        }
        app_.sound(0x20);   // ...and returns the carriage
        wait(15);
        screen.blit(work, x - 2, n * 8 + 0x4A, 0x20, 10, x - 2, 0x99);
        fill(x - 1, 0xA3, 0x20, 4, 7);
        if (keyPending()) break;
    }
    scroll(n);
    fill(0x32, 0x9B, 0xDC, 8, 15);
    {
        const Image tmp = screen;
        screen.blit(tmp, 0x31, 0x8E - n * 8, 0xDE, n * 8 + 0x15, 0x31, 0x88 - n * 8);
    }
    fill(0x32, 0x9B, 0xDC, 8, 15);
    drawSprite(0x6F, 0x34, 0x9A);
    waitKey();
    screen = back;
}

}  // namespace redux
