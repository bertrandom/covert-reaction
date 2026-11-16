// What the game library's routines draw and wait for when the mini-games call
// them during a case (TAC's dossiers and clue cards, BUG's intercepts), as a list
// of steps the program replays on its own screen when the routine returns.
//
// The routines run in GAME's library port (game/Sim.h), which tells its SimUi
// what it draws and waits for, in the original's order, while it changes the
// tables and draws its random numbers. LibRecorder is that SimUi: it records
// each drawing call as a step (drawn later on a LibCanvas, the program's g_gfx
// page with font 1 and draw_face's sprites), each wait_key_or_button as Key and
// each show_menu as Menu, so the logic runs to its end at once, with the
// random() calls in the original's order, and the screen catches up after. The
// program's own draw_intercept_popup (each program links its own) is a hook.
// No SDL.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/Image.h"
#include "formats/Font.h"
#include "game/Face.h"
#include "game/Sim.h"

namespace redux {

// The program's screen port: g_gfx's page, font 1, draw_sprite_a's icons and
// g_gfx->fillcolor (which the drawing changes as the original's does).
struct LibCanvas {
    Image& img;
    const Font& font;
    const FaceSprites& faces;
    int& fillcolor;
};

// Draw runs a drawing call; Key is wait_key_or_button; Menu show_menu(text, x, y);
// Retrace wait_retrace(n); Popup the program's draw_intercept_popup with
// text_buf = text and face_shown = n.
struct LibStep {
    enum class Kind { Draw, Key, Menu, Retrace, Popup } kind = Kind::Draw;
    std::function<void(LibCanvas&)> draw;
    std::string text;
    int x = 0, y = 0, n = 0;
};

class LibRecorder : public SimUi {
public:
    std::vector<LibStep>* out = nullptr;            // where the steps go
    Sim* sim = nullptr;                             // (face_shown is cleared by draw_portrait)
    std::vector<std::string>* trace = nullptr;      // one line per event shown (tests)
    // draw_intercept_popup (a Popup event); the default records a Popup step.
    std::function<void(const SimEvent&)> popup;

    int event(const SimEvent& e) override;
    void retrace(int n) override { step(LibStep::Kind::Retrace, {}, 0, 0, n); }
    void pageFrame(const std::string& title, int color) override;
    void text(const std::string& s, int x, int y, int c) override;
    void textOpaque(const std::string& s, int x, int y, int c) override;
    void line(int x1, int y1, int x2, int y2, int c) override;
    void wrapped(const std::string& s, int width, int x, int y, int c) override;
    void portrait(int face, int x, int y) override;
    void sprite(int n, int x, int y) override;
    void cardPanel(int x, int y) override;
    void bevelBox(int x, int y, int w, int h, int c) override;
    void fillRect(int x, int y, int w, int h, int c) override;
    void textCentered(const std::string& s, int x, int y, int c);   // draw_text_centered

    void step(LibStep::Kind k, std::string text = {}, int x = 0, int y = 0, int n = 0);
    void draw(std::function<void(LibCanvas&)> f);
    void log(std::string s) {
        if (trace) trace->push_back(std::move(s));
    }
};

// show_master_plan(org, reveal) (GAMELIB PERSON.C), drawn through the recorder.
void libShowMasterPlan(Sim& sim, LibRecorder& ui, int org, int reveal);

}  // namespace redux
