#include "app/Menu.h"

namespace reaction {

Menu::Menu(const Font& font, std::string text, int x, int y, TextWindowOptions opt)
    : font_(&font), text_(std::move(text)), x_(x), y_(y), opt_(opt),
      layout_(layoutTextWindow(font, text_, x, y)), sel_(opt.selected) {
    if (layout_.items <= 0 || layout_.firstItem < 0 || opt_.showOk || opt_.noSelect) sel_ = -1;
    else if (sel_ < 0 || sel_ >= layout_.items) sel_ = 0;
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

void Menu::handle(const SDL_Event& e, SDL_Point mouse) {
    if (flash_ >= 0 || done_) return;
    const bool hasItems = layout_.items > 0 && layout_.firstItem >= 0 && !opt_.showOk && !opt_.noSelect;
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
        case SDLK_DOWN:
            if (hasItems && sel_ < layout_.items - 1) ++sel_;
            break;
        case SDLK_UP:
            if (hasItems && sel_ > 0) --sel_;
            break;
        case SDLK_ESCAPE:
            choose(-1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
        case SDLK_F1:
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
}

}  // namespace reaction
