#include "app/Menu.h"

#include "app/App.h"
#include "app/DosKey.h"
#include "app/Scene.h"

namespace reaction {

TextWindowHotkeys appHotkeys(App& app, Scene& scene) {
    TextWindowHotkeys h;
    h.options = [&app] { return static_cast<int>(app.game.world.hdr.options); };
    h.setOptions = [&app](int v) {
        auto& o = app.game.world.hdr.options;
        const bool changedAnim = ((o ^ v) & 8) != 0;
        o = static_cast<std::int16_t>(v);
        if (changedAnim) {     // the main menu's "Animations" option follows Alt-A
            app.settings.animations = (v & 8) != 0;
            app.saveSettings();
        }
    };
    h.sound = [&app](int n) { app.sound(n); };
    h.quit = [&scene] { scene.quit = true; };
    return h;
}

Menu::Menu(const Font& font, std::string text, int x, int y, TextWindowOptions opt)
    : font_(&font), text_(std::move(text)), x_(x), y_(y), opt_(opt),
      layout_(layoutTextWindow(font, text_, x, y)), sel_(opt.selected) {
    if (layout_.items <= 0 || layout_.firstItem < 0 || opt_.showOk || opt_.noSelect) sel_ = -1;
    else if (sel_ < 0 || sel_ >= layout_.items) sel_ = 0;
}

Menu& Menu::setHotkeys(TextWindowHotkeys h) {
    hotkeys_ = std::move(h);
    return *this;
}

int Menu::itemAt(SDL_Point p) const {
    if (layout_.firstItem < 0) return -1;
    if (p.x < x_ || p.x > layout_.boxX2) return -1;
    int line = (p.y - y_ - 4) / 8;
    int item = line - layout_.firstItem;
    if (p.y - y_ - 4 < 0 || item < 0 || item >= layout_.items) return -1;
    return item;
}

void Menu::choose(int item) {
    if (flash_ >= 0 || done_) return;
    result_ = item;
    if (item >= 0) flash_ = 20;      // text_window: recolor to 15, wait_retrace(20)
    else done_ = true;
}

// case 0x1E00 / 0x1F00 / 0x2400: options ^= bit; then
// "if ((int)options > z) play_sound(20); else play_sound(19);".
void Menu::toggle(int bit) {
    if (!hotkeys_.options || !hotkeys_.setOptions) return;
    const int z = static_cast<std::int16_t>(hotkeys_.options());
    const int now = static_cast<std::int16_t>(z ^ bit);
    hotkeys_.setOptions(now);
    if (hotkeys_.sound) hotkeys_.sound(now > z ? 20 : 19);
}

void Menu::handle(const SDL_Event& e, SDL_Point mouse) {
    if (confirmingQuit()) {
        confirm_->handle(e, mouse);
        return;
    }
    if (flash_ >= 0 || done_) return;
    const bool hasItems = layout_.items > 0 && layout_.firstItem >= 0 && !opt_.showOk && !opt_.noSelect;
    if (e.type == SDL_KEYDOWN) {
        switch (dosKey(e)) {
        case 0x5000:                   // down (keypad 2 too: NumLock off)
            if (hasItems && sel_ < layout_.items - 1) ++sel_;
            break;
        case 0x4800:                   // up (keypad 8)
            if (hasItems && sel_ > 0) --sel_;
            break;
        case 0x1E00:                   // Alt-A: animations
            toggle(8);
            break;
        case 0x1F00:                   // Alt-S: sound
            toggle(4);
            break;
        case 0x2400:                   // Alt-J: joystick
            // The original also calibrates the joystick when the bit goes on
            // (drv_joy_calibrate); the port has no joystick, so only the bit changes.
            toggle(0x10);
            break;
        case 0x1000: {                 // Alt-Q: quit
            if (!hotkeys_.options) break;
            // anim_active = menu_flag = 0; the window at (x, y & 0FEh) with the
            // globals this one leaves (default_sel, disabled, show_ok).
            TextWindowOptions o;
            o.selected = opt_.selected;
            o.disabled = opt_.disabled;
            o.showOk = opt_.showOk;
            o.fillColor = opt_.fillColor;
            confirm_ = std::make_shared<Menu>(*font_, "Are you sure\nyou want to Quit?\n No\n Yes\n", x_, y_ & 0xFE, o);
            confirm_->setHotkeys(hotkeys_);
            confirmClosed_ = false;
            break;
        }
        case 27:                       // Esc
            choose(-1);
            break;
        case 13:
        case ' ':
        case 0x3B00:                   // F1
            choose(hasItems ? sel_ : -1);
            break;
        default:
            break;
        }
    } else if (e.type == SDL_MOUSEMOTION && hasItems) {
        if (int it = itemAt(mouse); it >= 0) sel_ = it;
    } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        if (!hasItems) choose(-1);
        else if (int it = itemAt(mouse); it >= 0) { sel_ = it; choose(it); }
    }
}

void Menu::tick() {
    if (confirmingQuit()) {
        confirm_->tick();
        if (auto r = confirm_->result()) {
            confirmClosed_ = true;
            if (*r == 1) {             // Yes: quit_flag = -1; ENV[0] = quit_flag; set_exit_state(); exit()
                // exit() never returns: this window never gives a result (the
                // scene would act on it before the app sees it quit).
                if (hotkeys_.quit) hotkeys_.quit();
                return;
            }
            // No: anim_active = sav, then on into the Esc case.
            sel_ = -1;
            choose(-1);
        }
        return;
    }
    if (flash_ > 0 && --flash_ == 0) done_ = true;
}

void Menu::draw(Image& frame) const {
    TextWindowOptions o = opt_;
    o.selected = -1;
    drawTextWindow(frame, *font_, text_, x_, y_, o);
    if (sel_ >= 0) {
        const auto box = textWindowColors(y_, opt_).box;
        if (flash_ >= 0) highlightItem(frame, layout_, x_, y_, sel_, box, 15);
        else highlightItem(frame, layout_, x_, y_, sel_, box, kSelectionColor);
    }
    if (confirm_) confirm_->draw(frame);
}

}  // namespace reaction
