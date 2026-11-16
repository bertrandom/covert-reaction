// GAME's screen as the city loop draws it: the pages (g_gfx, back_page,
// work_page), the library's drawing calls, text windows with their scene
// animations and portraits (TEXTWIN.C text_window, UI.C set_scene_background),
// the status bar (TIME.C draw_status_bar), the teletype (TELETYPE.C
// incoming_message, draw_teletype_paper), pictures and the city pictures of
// cities.cat (BRIEFING.C load_city_picture, show_city_from_catalog).
//
// The program side (GAME's code, on a GameThread) calls the drawing functions
// and the blocking ones (textWindow, waitKey, getKey, wait), which hand a
// request to the scene and yield until it is answered. The scene side (handle,
// tick, draw) runs the request: counts retraces, takes keys, runs the Menu widget
// and the .PAN animation behind it. It is also the Sim's UI (SimUi), so the
// simulation's routines draw and wait the same way.
#pragma once

#include <SDL.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "app/App.h"
#include "app/Dissolve.h"
#include "app/GameThread.h"
#include "app/Menu.h"
#include "app/Scene.h"
#include "core/Image.h"
#include "formats/Pan.h"
#include "game/Face.h"
#include "game/Sim.h"

namespace reaction {

// process_events ended the case: GAME exits (to FINAL's debriefing) from
// wherever it was. Thrown through the program's stack by the UI.
struct CaseOverExit {};

class CityUi : public SimUi {
public:
    CityUi(App& app, GameThread& thread);

    void setSim(Sim* sim) { sim_ = sim; }
    // The scene showing this screen: the text windows' Alt-Q (Yes) quits it.
    void setScene(Scene* scene) { scene_ = scene; }
    Sim& sim() { return *sim_; }
    const Font& font() const { return app_.fonts.font(1); }
    const FaceSprites& faces() const { return faces_; }

    // ---- the pages and the port (program side) ----
    Image screen{320, 200, 5};   // page 0, g_gfx
    Image back{320, 200, 0};     // page 1, back_page
    Image work{320, 200, 0};     // page 2, work_page
    int fillColor = 5;           // g_gfx->fillcolor
    int statusY = 174;           // status_y
    int screenW = 320;           // screen_width
    int animX = 0;               // anim.x (set_scene_background)
    int portraitX = 0, portraitY = 0;
    // options & 8 (animations; App::syncAnimations keeps it = Settings::animations)
    bool animations() const { return (app_.game.world.hdr.options & 8) != 0; }

    // ---- blocking calls (program side) ----
    // text_window(text, x, y): the chosen item, -1 (Esc, or a window without
    // items). Plays the pending scene animation behind it.
    int textWindow(const std::string& text, int x, int y, unsigned flags = 0);
    int showMenu(const std::string& text, int x, int y, unsigned flags = 0) { return textWindow(text, x, y, flags); }
    void waitKey();              // wait_key_or_button
    int getKey();                // get_key: the next key (DOS code)
    bool keyPending() const;     // !drv_kbhit()
    void flushInput();           // flush_input
    void wait(int retraces);     // wait_retrace(n)
    // A paused window (dossier_state) left up while the next program loads: the
    // original showed it for as long as the load took; this holds it `retraces`
    // or until a key.
    void holdPaused(int retraces = 2 * kTicksPerSecond);
    // drv_dissolve_page: the back page dissolves onto the screen.
    void dissolveBackPage();
    void incomingMessage(const std::string& title, const std::string& text);   // incoming_message
    // Runs another scene in the middle of the program (the save/load menus of the
    // hotel): `make(last, then)` builds it with the screen as it is; the scene
    // ends when it moves on to the scene `then` gives it. Returns the frame it
    // left. A scene quitting the game makes childQuit() true (and yields forever).
    using SceneMaker = std::function<std::unique_ptr<Scene>(const Frame& last,
                                                            std::function<std::unique_ptr<Scene>(const Frame&)> then)>;
    Frame runScene(const SceneMaker& make);
    bool childQuit() const { return childQuit_; }

    // ---- drawing (program side) ----
    void clearScreenNow();       // clear_screen
    void clearPlayAreaNow();     // clear_play_area
    void drawStatusBar(const std::string& dateTime);
    void drawPageFrame(const std::string& title, int color);
    void drawTeletypePaper();
    void drawText(const std::string& s, int x, int y, int color);          // draw_text (opaque)
    void drawTextT(const std::string& s, int x, int y, int color);         // draw_text_transparent
    void drawTextCentered(const std::string& s, int x, int y, int color);  // draw_text_centered
    int drawWrapped(const std::string& s, int width, int x, int y, int color);   // draw_wrapped_text
    void drawPortrait(int face, int x, int y);                             // draw_portrait
    void drawFace(int face, int x, int y);                                 // draw_face_parts
    void drawSprite(int n, int x, int y);                                  // draw_sprite_a
    void drawBevel(int x, int y, int w, int h, int color);                 // draw_bevel_box
    void drawOutline(int x, int y, int w, int h, int color);               // draw_rect_outline
    void drawLineC(int x1, int y1, int x2, int y2, int color);             // draw_line
    void fill(int x, int y, int w, int h, int color);                      // gfx_fill_rect (screen)
    void recolor(int x, int y, int w, int h, int from, int to);            // drv_recolor_rect
    // load_picture_swapped(name, page): page 0 the screen, 1 the back page.
    void loadPicture(const std::string& name, int page);
    // set_scene_background: false (and the screen cleared) with animations off.
    bool setSceneBackground(const std::string& name, int x, int y);
    void setSceneVar(int i, int v);
    // play_animation(name, x, arg): a whole .PAN, then the game palette.
    void playAnimation(const std::string& name, int x);
    // load_city_picture(c, x, y)
    void loadCityPicture(int city, int x, int y);

    // ---- SimUi ----
    int event(const SimEvent& e) override;
    void statusBar(const std::string& dateTime) override { drawStatusBar(dateTime); }
    void retrace(int n) override { wait(n); }
    void pageFrame(const std::string& title, int color) override { drawPageFrame(title, color); }
    void teletypePaper() override { drawTeletypePaper(); }
    void text(const std::string& s, int x, int y, int color) override { drawTextT(s, x, y, color); }
    void textOpaque(const std::string& s, int x, int y, int color) override { drawText(s, x, y, color); }
    void line(int x1, int y1, int x2, int y2, int color) override { drawLineC(x1, y1, x2, y2, color); }
    void wrapped(const std::string& s, int width, int x, int y, int color) override { drawWrapped(s, width, x, y, color); }
    void portrait(int face, int x, int y) override { drawPortrait(face, x, y); }
    void sprite(int n, int x, int y) override { drawSprite(n, x, y); }
    void cardPanel(int x, int y) override;
    void bevelBox(int x, int y, int w, int h, int color) override { drawBevel(x, y, w, h, color); }
    void clearPlayArea() override { clearPlayAreaNow(); }
    void clearScreen() override { clearScreenNow(); }
    void fillRect(int x, int y, int w, int h, int color) override { fill(x, y, w, h, color); }
    void toBackPage(int x, int y, int w, int h) override { back.blit(screen, x, y, w, h, x, y); }
    void fromBackPage(int x, int y, int w, int h) override { screen.blit(back, x, y, w, h, x, y); }
    void screenWidth(int w) override { screenW = w; }
    void portraitPos(int x, int y) override { portraitX = x, portraitY = y; }
    void sceneVar(int i, int v) override { setSceneVar(i, v); }
    bool sceneBackground(const std::string& name, int x, int y) override { return setSceneBackground(name, x, y); }
    void faceParts(int face, int x, int y) override { drawFace(face, x, y); }

    // ---- scene side ----
    void handle(const SDL_Event& e);
    // Runs the pending request one retrace; true when it is answered (the
    // scene then resumes the program).
    bool tick();
    void draw(Frame& f) const;
    bool waiting() const { return req_.kind != Request::None; }

private:
    struct Request {
        enum Kind { None, Delay, Key, Menu, Child, Dissolve } kind = None;
        int ticks = 0;
        int result = -1;
        bool flushAfter = false;       // wait_key_or_button flushes the rest
        bool keyEnds = false;          // Delay: a key ends it (holdPaused)
    };
    struct SceneAnim {
        std::string name;
        int x = 0, y = 0;
    };

    void ask(Request r);               // program side: post and yield
    Image composite() const;           // the screen with the animation behind the window
    void playAnimSounds();             // the animation's queued sounds

    App& app_;
    GameThread& thread_;
    Sim* sim_ = nullptr;
    Scene* scene_ = nullptr;
    FaceSprites faces_;
    Palette palette_;

    std::optional<SceneAnim> pending_;            // set_scene_background
    std::array<int, PanPlayer::kVars> vars_{};    // set_scene_var
    std::unique_ptr<PanPlayer> anim_;             // playing behind a text window
    int animSw_ = 320;                            // screen_width while it plays
    // A window in the bottom strip (odd y >= 144) over an animation: text_window
    // fills only the strip, and the animation shows everywhere above it.
    int animStripY_ = -1;
    bool bldFlag_ = false;                        // bld_flag: a building's .PAN keeps the status bar
    bool playing_ = false;                        // play_animation: the .PAN alone

    Request req_;
    reaction::Dissolve dissolve_;                 // dissolveBackPage
    std::deque<SDL_Event> keys_;                  // typed ahead
    std::optional<reaction::Menu> menu_;
    std::string menuText_;
    std::unique_ptr<Scene> child_;                // runScene
    Frame childLast_;
    bool childQuit_ = false;
    int cityPicShown_ = -1;                       // anim_handle: the city picture on the work page
    int cityPicW_ = 0, cityPicH_ = 0;
};

}  // namespace reaction
