// A text window / menu with the original's interaction (TEXTWIN.C text_window):
// Up/Down move the highlight, Enter/Space/F1 choose, Esc cancels (-1); the chosen
// item flashes white for 20 retraces before the menu closes. The mouse hovers and
// clicks items too.
#pragma once

#include <SDL.h>

#include <optional>
#include <string>

#include "core/Image.h"
#include "formats/Font.h"
#include "formats/TextWindow.h"

namespace reaction {

class Menu {
public:
    // `text`: lines ended by '\n'; lines starting with a space are items.
    Menu(const Font& font, std::string text, int x, int y, TextWindowOptions opt = {});

    // `frame` is the screen the menu is drawn over; `mouse` maps window to frame
    // coordinates (Screen::toFrame).
    void handle(const SDL_Event& e, SDL_Point mouse);
    void tick();
    void draw(Image& frame) const;

    // The chosen item (0-based) or -1 (Esc / no items), once the menu is done.
    std::optional<int> result() const { return done_ ? std::optional<int>(result_) : std::nullopt; }
    const TextWindowLayout& layout() const { return layout_; }

private:
    int itemAt(SDL_Point p) const;
    void choose(int item);

    const Font* font_;
    std::string text_;
    int x_, y_;
    TextWindowOptions opt_;
    TextWindowLayout layout_;
    int sel_;
    int flash_ = -1;          // retraces left of the chosen item's flash
    int result_ = -1;
    bool done_ = false;
};

}  // namespace reaction
