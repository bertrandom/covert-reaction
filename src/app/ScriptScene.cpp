#include "app/ScriptScene.h"

#include "app/DosKey.h"
#include "formats/McgaPalette.h"
#include "formats/Pic.h"
#include "game/Paper.h"

namespace reaction {

namespace {

constexpr double kTick = 1.0 / kTicksPerSecond;

}  // namespace

ScriptScene::ScriptScene(App& app, const Frame& start, Step first)
    : app_(app), faces_(FaceSprites::load(app.data)), palette_(mcgaPalette(palettes::kGame)), screen_(start.image) {
    // load_picture_swapped("icons.pic", 2): the items' icons (icon_page).
    icons_ = Image(320, 200, 0);
    auto pic = decodePicture(app.data.read("icons.pic"));
    icons_.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
    if (screen_.width() != 320 || screen_.height() != 200) screen_ = Image(320, 200, 0);
    if (first) queue_.push_back(std::move(first));
}

Frame ScriptScene::frame() const {
    Frame f;
    f.image = (anim_ && animating_) ? anim_->frame() : screen_;
    f.palette = palette_;
    return f;
}

bool ScriptScene::isKeyOrClick(const SDL_Event& e) const {
    return (e.type == SDL_KEYDOWN && dosKey(e) >= 0) || e.type == SDL_MOUSEBUTTONDOWN;
}

void ScriptScene::insert(std::vector<Step> steps) {
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) queue_.push_front(std::move(*it));
}

ScriptScene::Step ScriptScene::menuStep(std::string text, int x, int y, std::function<void(int)> done, bool clear,
                                        int selected) {
    return [=, text = std::move(text), done = std::move(done)](ScriptScene& s) {
        TextWindowOptions o;
        o.fillColor = 5;
        o.selected = selected;
        s.menu_.emplace(s.app_.fonts.font(1), text, x, y, o);
        s.onMenu_ = done;
        s.clearAfter_ = clear;
        if (s.armed_ && (s.app_.game.world.hdr.options & 8)) {
            // text_window starts the armed animation from the screen as it is.
            s.anim_ = std::make_unique<PanPlayer>(PanFile::parse(s.app_.data.read("briefing.pan")), PanOptions{},
                                                  &s.screen_);
            s.anim_->setVar(0, s.armedVar_);
            s.anim_->step();
            s.animating_ = true;
        }
        s.armed_ = false;
        s.mode_ = Mode::Menu;
    };
}

ScriptScene::Step ScriptScene::pausedStep(std::string text, int x, int y) {
    return [=, text = std::move(text)](ScriptScene& s) {
        TextWindowOptions o;
        o.fillColor = 5;
        o.selected = -1;
        drawTextWindow(s.screen_, s.app_.fonts.font(1), text, x, y, o);
    };
}

ScriptScene::Step ScriptScene::storyStep(int var) {
    return [var](ScriptScene& s) {
        if (!(s.app_.game.world.hdr.options & 8)) {
            s.screen_.fill(5);
            return;
        }
        s.armed_ = true;
        s.armedVar_ = var;
    };
}

ScriptScene::Step ScriptScene::pagesStep(std::vector<Page> p, std::function<void(std::size_t, bool)> done) {
    return [p = std::move(p), done = std::move(done)](ScriptScene& s) mutable {
        s.pages_ = std::move(p);
        s.page_ = 0;
        s.onPages_ = std::move(done);
        s.mode_ = Mode::Pages;
        s.startPage();
    };
}

void ScriptScene::startPage() {
    while (page_ < pages_.size()) {
        const Page& p = pages_[page_];
        for (const auto& o : p.ops) apply(o);
        switch (p.wait) {
        case Page::Wait::None:
            ++page_;
            continue;
        case Page::Wait::Retrace:
            retrace_ = p.retraces;
            return;
        default:
            return;   // a key
        }
    }
    mode_ = Mode::Idle;
    auto done = std::move(onPages_);
    if (done) done(pages_.empty() ? 0 : pages_.size() - 1, false);
}

ScriptScene::Step ScriptScene::retraceStep(int n) {
    return [n](ScriptScene& s) {
        s.retrace_ = n;
        s.mode_ = Mode::Retrace;
    };
}

ScriptScene::Step ScriptScene::anyKeyStep() {
    return [](ScriptScene& s) { s.mode_ = Mode::AnyKey; };
}

ScriptScene::Step ScriptScene::vacationStep(int n, std::function<bool(int)> extra) {
    return [n, extra = std::move(extra)](ScriptScene& s) {
        const Vacation v = vacationFor(n);
        const bool maxine = (s.app_.game.world.hdr.options & 2) != 0;
        s.pics_ = loadVacation(s.app_.data, v, maxine, extra && extra(v.kind));
        s.app_.sound(v.kind + 40);   // play_sound(kind + 40): the vacation tune
        // icon_page: the background, the extra picture, the hand; copied whole.
        s.screen_ = s.pics_.back;
        if (!s.pics_.extra.empty()) s.screen_.blit(s.pics_.extra, 0, 0, 320, 200, 0, 0, 0);
        s.screen_.blit(s.pics_.hand, 0, 0, 320, 200, 0, 0, 0);
        s.vstep_ = 0;
        s.dissolve_ = Dissolve();
        s.mode_ = Mode::Vacation;
    };
}

ScriptScene::Step ScriptScene::finishStep(std::function<std::unique_ptr<Scene>(const Frame&)> next) {
    return [next = std::move(next)](ScriptScene& s) {
        s.mode_ = Mode::Done;
        s.next_ = next(s.frame());
    };
}

void ScriptScene::apply(const DrawOp& o) {
    using K = DrawOp::Kind;
    const Font& font = app_.fonts.font(1);
    Image& s = screen_;
    const auto c8 = [](int v) { return static_cast<std::uint8_t>(v); };
    switch (o.kind) {
    case K::Fill: fillRect(s, o.a, o.b, o.c, o.d, c8(o.e)); break;
    case K::Recolor: recolorRect(s, o.a, o.b, o.c, o.d, c8(o.e), c8(o.f)); break;
    case K::Line: drawLine(s, o.a, o.b, o.c, o.d, c8(o.e)); break;
    case K::Text: font.draw(s, o.a, o.b, o.text, {0, c8(o.c), 0}); break;
    case K::TextOpaque: font.draw(s, o.a, o.b, o.text, {1, c8(o.c), c8(o.d)}); break;
    case K::TextCentered: drawTextCentered(s, font, o.text, o.a, o.b, c8(o.c)); break;
    case K::Wrapped: drawWrappedText(s, font, o.text, o.a, o.b, o.c, c8(o.d)); break;
    case K::FaceParts: drawFaceParts(s, faces_, o.a, o.b, o.c); break;
    case K::Sprite:
        if (o.a >= 0 && o.a < FaceSprites::kCount) faces_.draw(s, o.a, o.b, o.c);
        break;
    case K::FillFromPixel: {
        const std::uint8_t col = (o.a >= 0 && o.b >= 0 && o.a < 320 && o.b < 200) ? s.at(o.a, o.b) : 0;
        fillRect(s, o.c, o.d, o.e, o.f, col);
        break;
    }
    case K::Icon: s.blit(icons_, (o.a & 0xFF) << 4, 0, 16, 16, o.b, o.c); break;
    case K::PageFrame: drawPageFrame(s, font, faces_, o.text, c8(o.a)); break;
    case K::CardPanel: drawCardPanel(s, o.a, o.b); break;
    case K::Portrait: drawPortrait(s, faces_, o.a, o.b, o.c); break;
    case K::FaceFor: drawFaceFor(s, faces_, o.a, o.b, o.c); break;
    case K::Picture: {
        auto pic = decodePicture(app_.data.read(o.text));
        s.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
        break;
    }
    }
}

void ScriptScene::handle(const SDL_Event& e) {
    switch (mode_) {
    case Mode::Menu:
        menu_->handle(e, app_.mouse(e));
        break;
    case Mode::AnyKey:
        if (isKeyOrClick(e)) mode_ = Mode::Idle;
        break;
    case Mode::Pages: {
        if (page_ >= pages_.size() || !isKeyOrClick(e)) break;
        const Page& p = pages_[page_];
        if (p.wait == Page::Wait::Retrace) break;
        if (p.wait == Page::Wait::KeyOrEsc && dosKey(e) == 27) {
            mode_ = Mode::Idle;
            auto done = std::move(onPages_);
            if (done) done(page_, true);
            break;
        }
        ++page_;
        startPage();
        break;
    }
    default:
        break;
    }
}

void ScriptScene::tick() {
    if (anim_ && animating_) anim_->advance(kTick);
    switch (mode_) {
    case Mode::Menu: {
        menu_->tick();
        auto r = menu_->result();
        if (!r) return;
        if (anim_ && animating_) {
            // text_window ends the animation (anim_stop): its last frame stays.
            screen_ = anim_->frame();
            fillRect(screen_, 4, 149, 312, 198 - 149, 5);
        }
        animating_ = false;
        anim_.reset();
        menu_->draw(screen_);
        if (clearAfter_) fillRect(screen_, 4, 149, 312, 48, 5);   // clear_text_area
        menu_.reset();
        mode_ = Mode::Idle;
        auto done = std::move(onMenu_);
        if (done) done(*r);
        break;
    }
    case Mode::Retrace:
        if (--retrace_ <= 0) mode_ = Mode::Idle;
        break;
    case Mode::Pages:
        if (page_ < pages_.size() && pages_[page_].wait == Page::Wait::Retrace && --retrace_ <= 0) {
            ++page_;
            startPage();
        }
        break;
    case Mode::Vacation:
        if (dissolve_.active()) {
            dissolve_.tick(screen_);
            break;
        }
        if (vstep_ < pics_.frames.size()) {
            ++vstep_;
            Image page = pics_.back;
            for (std::size_t m = 0; m < vstep_; ++m) page.blit(pics_.frames[m], 0, 0, 320, 200, 0, 0, 0);
            page.blit(pics_.hand, 0, 0, 320, 200, 0, 0, 0);
            dissolve_ = Dissolve(std::move(page));
            break;
        }
        pics_ = {};
        mode_ = Mode::Idle;
        break;
    default:
        break;
    }
    // Run the steps that don't wait.
    for (int guard = 0; mode_ == Mode::Idle && !queue_.empty() && guard < 1000; ++guard) {
        Step s = std::move(queue_.front());
        queue_.pop_front();
        s(*this);
    }
}

void ScriptScene::draw(Frame& f) {
    f.palette = palette_;
    if (anim_ && animating_) {
        f.image = anim_->frame();
        // draw_text_window with the animation on, y >= 144: the strip below.
        fillRect(f.image, 4, 149, 312, 198 - 149, 5);
    } else {
        f.image = screen_;
    }
    if (menu_) menu_->draw(f.image);
}

}  // namespace reaction
