// The front end, as FINAL.EXE's main() runs it on a fresh start: the MPS Labs
// picture while the title loads, TITLE2.PAN, CREDITS.PAN, then the main menu.
#include <memory>

#include "app/FrontEnd.h"
#include "app/Scenes.h"
#include "app/Menu.h"
#include "formats/McgaPalette.h"
#include "formats/Pan.h"
#include "formats/Pic.h"

namespace redux {

namespace {

constexpr double kTick = 1.0 / kTicksPerSecond;

bool isKeyOrClick(const SDL_Event& e) {
    return (e.type == SDL_KEYDOWN && !(e.key.keysym.mod & KMOD_ALT)) || e.type == SDL_MOUSEBUTTONDOWN;
}

class MainMenuScene : public Scene {
public:
    MainMenuScene(App& app, Image background)
        : app_(app), background_(std::move(background)), palette_(mcgaPalette(palettes::kGame)) {
        openMain();
    }

    void handle(const SDL_Event& e) override { menu_->handle(e, app_.mouse(e)); }

    void tick() override {
        menu_->tick();
        auto r = menu_->result();
        if (!r) return;
        if (notice_) {           // a "not ported yet" notice was closed
            notice_ = false;
            openMain();
            return;
        }
        // FINAL.C: -1 (Esc) and 0 create a new character.
        static const char* what[] = {"Creating a character", "Loading a saved game",
                                     "Practising a skill", "The Hall of Fame"};
        int sel = *r < 0 ? 0 : *r;
        std::string text = std::string(what[sel]) + "\nisn't ported yet.\n";
        TextWindowOptions opt;
        opt.showOk = true;
        menu_.emplace(app_.fonts.font(1), text, 96, 90, opt);
        notice_ = true;
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        f.image = background_;
        menu_->draw(f.image);
    }

private:
    void openMain() {
        menu_.emplace(app_.fonts.font(1),
                      "Do you want to...\n Create a New Character\n Load a Saved Game\n"
                      " Practice a skill\n Review Hall of Fame\n",
                      80, 80);
    }

    App& app_;
    Image background_;
    Palette palette_;
    std::optional<Menu> menu_;
    bool notice_ = false;
};

// One full-screen animation (play_title_pan): any key or click skips it, Esc also
// skips the ones after it.
class TitleScene : public Scene {
public:
    explicit TitleScene(App& app) : app_(app) {
        // load_picture_swapped("labs.pic", 0): on screen while the title loads.
        auto labs = decodePicture(app.data.read("labs.pic"));
        screen_ = std::move(labs.image);
        palette_ = mcgaPalette(palettes::kGame);
    }

    void handle(const SDL_Event& e) override {
        if (!isKeyOrClick(e)) return;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) skipAll_ = true;
        skip_ = true;
    }

    void tick() override {
        if (phase_ == Phase::Labs) {
            if (++labsTicks_ >= kTicksPerSecond * 2 || skip_) start("title2.pan", Phase::Title);
            return;
        }
        if (skip_ || !player_->finished()) {
            if (!skip_) player_->advance(kTick);
            if (!skip_ && !player_->finished()) return;
        }
        screen_ = player_->frame();
        if (phase_ == Phase::Title && !skipAll_) start("credits.pan", Phase::Credits);
        else done_ = true;
    }

    void draw(Frame& f) override {
        if (player_ && phase_ != Phase::Labs) {
            f.image = player_->frame();
            f.palette = player_->palette();
        } else {
            f.image = screen_;
            f.palette = palette_;
        }
    }

    std::unique_ptr<Scene> next() override {
        if (!done_) return nullptr;
        return std::make_unique<MainMenuScene>(app_, screen_);
    }

private:
    enum class Phase { Labs, Title, Credits };

    void start(const char* name, Phase p) {
        // Each animation starts from what the screen shows (CREDITS has no
        // background of its own and draws over the end of TITLE2).
        Image from = player_ ? player_->frame() : screen_;
        player_ = std::make_unique<PanPlayer>(PanFile::parse(app_.data.read(name)), PanOptions{}, &from);
        player_->step();
        phase_ = p;
        skip_ = false;
    }

    App& app_;
    Image screen_;
    Palette palette_;
    std::unique_ptr<PanPlayer> player_;
    Phase phase_ = Phase::Labs;
    int labsTicks_ = 0;
    bool skip_ = false, skipAll_ = false, done_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeTitleScene(App& app) { return std::make_unique<TitleScene>(app); }

std::unique_ptr<Scene> makeMainMenuScene(App& app) {
    Image bg(320, 200, 0);
    return std::make_unique<MainMenuScene>(app, std::move(bg));
}

REDUX_SCENE("title", makeTitleScene);
REDUX_SCENE("main-menu", makeMainMenuScene);

}  // namespace redux
