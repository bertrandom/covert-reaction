// A scene that runs a sequence of the original's screen steps in order: text
// windows and menus (text_window), display-list pages (src/game/EndCase.h Page:
// drawing then wait_key_or_button / wait_retrace), the briefing animation that
// set_scene_background arms for the next text window, and the vacation snapshot.
// Steps are queued functions; a step may queue more (computed when reached, so
// the random() draws stay in the original's order). FINAL's debriefing and the
// save/load menus are built from it.
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/Dissolve.h"
#include "app/Menu.h"
#include "app/Scene.h"
#include "formats/Pan.h"
#include "formats/TextWindow.h"
#include "game/EndCase.h"
#include "game/Face.h"
#include "game/Fame.h"

namespace redux {

class ScriptScene : public Scene {
public:
    using Step = std::function<void(ScriptScene&)>;

    // `start`: the screen as the previous scene left it.
    ScriptScene(App& app, const Frame& start, Step first);

    void handle(const SDL_Event& e) override;
    void tick() override;
    void draw(Frame& f) override;
    std::unique_ptr<Scene> next() override { return std::move(next_); }

    // ---- steps ----
    // Queued after the ones already queued.
    void then(Step s) { queue_.push_back(std::move(s)); }
    // From inside a step: these run next, in order, before the rest of the queue
    // (a step that expands into others when it is reached).
    void insert(std::vector<Step> steps);

    // show_menu(text, x, y): `done` gets the item (-1 for Esc or no items). The
    // window stays drawn on the screen afterwards; `clear` then runs
    // clear_text_area (the briefing's text strip).
    static Step menuStep(std::string text, int x, int y, std::function<void(int)> done = {}, bool clear = false,
                         int selected = 0);
    // A window drawn without waiting (dossier_state / TEXTWIN's paused).
    static Step pausedStep(std::string text, int x, int y);
    // set_scene_var(0, var); set_scene_background("briefing.pan"): the animation
    // plays under the next text window (only it). Without animations (options
    // bit 8) the screen is cleared to colour 5.
    static Step storyStep(int var);
    // The pages in order; `done(i, esc)`: the last page shown, Esc on a KeyOrEsc wait.
    static Step pagesStep(std::vector<Page> p, std::function<void(std::size_t, bool)> done = {});
    static Step retraceStep(int n);     // wait_retrace(n)
    static Step anyKeyStep();           // wait_key_or_button()
    // vacation_snapshot(n, 1)'s pictures (after its caption): the background, the
    // extra picture if `extra` (its random(3) is drawn when the step runs), the
    // hand, then the frames dissolving in.
    static Step vacationStep(int n, std::function<bool(int kind)> extra);
    // Ends the scene: the next one, given the screen as it is.
    static Step finishStep(std::function<std::unique_ptr<Scene>(const Frame&)> next);

    void menu(std::string text, int x, int y, std::function<void(int)> done = {}, bool clear = false,
              int selected = 0) {
        then(menuStep(std::move(text), x, y, std::move(done), clear, selected));
    }
    void finish(std::function<std::unique_ptr<Scene>(const Frame&)> next) { then(finishStep(std::move(next))); }
    void disarm() { armed_ = false; }   // anim_active = 0

    Image& screen() { return screen_; }
    App& app() { return app_; }
    Frame frame() const;
    void apply(const DrawOp& op);       // one display-list operation on the screen

private:
    enum class Mode { Idle, Menu, Pages, Retrace, AnyKey, Vacation, Done };
    bool isKeyOrClick(const SDL_Event& e) const;
    void startPage();

    App& app_;
    FaceSprites faces_;
    Image icons_;
    Palette palette_;
    Image screen_{320, 200, 5};
    std::deque<Step> queue_;
    Mode mode_ = Mode::Idle;

    std::optional<Menu> menu_;
    std::function<void(int)> onMenu_;
    bool clearAfter_ = false;

    bool armed_ = false;
    int armedVar_ = 4;
    std::unique_ptr<PanPlayer> anim_;
    bool animating_ = false;

    std::vector<Page> pages_;
    std::size_t page_ = 0;
    std::function<void(std::size_t, bool)> onPages_;
    int retrace_ = 0;

    VacationPictures pics_;
    std::size_t vstep_ = 0;
    Dissolve dissolve_;

    std::unique_ptr<Scene> next_;
};

}  // namespace redux
