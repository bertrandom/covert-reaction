// The front end, as FINAL.EXE's main() runs it on a fresh start: the MPS Labs
// picture while the title loads, TITLE2.PAN, CREDITS.PAN, then the main menu.
#include <memory>

#include "app/DosKey.h"
#include "app/FrontEnd.h"
#include "app/LoadGame.h"
#include "app/Scenes.h"
#include "app/Menu.h"
#include "app/NewCharScenes.h"
#include "app/Options.h"
#include "formats/McgaPalette.h"
#include "formats/Pan.h"
#include "formats/Pic.h"
#include "game/NewChar.h"

namespace reaction {

namespace {

constexpr double kTick = 1.0 / kTicksPerSecond;

TextWindowOptions fill5(int selected = 0) {
    TextWindowOptions o;
    o.fillColor = 5;     // g_gfx->fillcolor = 5
    o.selected = selected;
    return o;
}

class MainMenuScene : public Scene {
public:
    MainMenuScene(App& app, Image background)
        : app_(app), background_(std::move(background)), screen_(background_),
          palette_(mcgaPalette(palettes::kGame)) {
        openMain();
    }

    void handle(const SDL_Event& e) override { menu_->handle(e, app_.mouse(e)); }

    void tick() override {
        menu_->tick();
        auto r = menu_->result();
        if (!r || next_) return;
        menu_->draw(screen_);         // a text window stays on the screen after it closes
        if (practiceStep_ == 1) {     // practice_menu: difficulty = show_menu(...)
            if (*r < 0) {
                // The original goes on with difficulty -1 (a bug); here Esc goes back.
                practiceStep_ = 0;
                screen_ = background_;
                openMain();
                return;
            }
            app_.practiceLevel = *r;
            // training_screen(1): training.pic, the practice menu over it.
            practiceStep_ = 2;
            screen_ = Image(320, 200, 0);
            const Picture pic = decodePicture(app_.data.read("training.pic"));
            screen_.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
            open(trainingMenu(true), 92, 25, fill5());
            return;
        }
        if (practiceStep_ == 2) {     // FINAL main: start_practice(sel); Esc (-1) gives 0, Combat
            practiceStep_ = 0;
            static const char* scenes[] = {"tac", "chase", "code", "bug"};
            next_ = makeScene(app_, scenes[*r < 0 || *r > 3 ? 0 : *r]);
            return;
        }
        app_.sound(1);                // FINAL main: play_sound(1) after show_menu (the title music stops)
        // FINAL.C: -1 (Esc) and 0 create a new character.
        int sel = *r < 0 ? 0 : *r;
        if (sel == 0 || sel == 3) {
            // The menu comes back over the screen the Hall of Fame leaves.
            auto back = [this](const Frame& last) { return makeMainMenuSceneOver(app_, last.image); };
            next_ = sel == 0 ? makeScene(app_, "new-character") : makeHallOfFameScene(app_, std::nullopt, back);
            return;
        }
        if (sel == 1) {               // load_game_menu
            Frame f;
            f.image = screen_;
            f.palette = palette_;
            next_ = makeFinalLoadGameScene(app_, f);
            return;
        }
        if (sel == 2) {               // practice_menu
            practiceStep_ = 1;
            open("Which difficulty level?\n Local Disturbance\n National Threat\n"
                 " Regional Conflict\n Global Crisis\n",
                 80, 128, fill5());
            return;
        }
        if (sel == 4) openOptions();   // (the port's own window: nothing stays behind)
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        f.image = screen_;
        menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override { return std::move(next_); }

private:
    // A text window with text_window's Alt keys.
    void open(std::string text, int x, int y, TextWindowOptions opt) {
        menu_.emplace(app_.fonts.font(1), std::move(text), x, y, opt).setHotkeys(appHotkeys(app_, *this));
    }

    void openMain() {
        open("Do you want to...\n Create a New Character\n Load a Saved Game\n"
             " Practice a skill\n Review Hall of Fame\n Options\n",
             80, 80, fill5());
    }

    // Covert Reaction's own Options window (app/Options.h), back to this menu.
    void openOptions(int selected = 0) {
        Frame f;
        f.image = background_;
        App& app = app_;
        Image bg = background_;
        next_ = std::make_unique<OptionsScene>(
            app_, std::move(f), [&app, bg] { return std::unique_ptr<Scene>(std::make_unique<MainMenuScene>(app, bg)); },
            false, selected);
    }

    App& app_;
    Image background_;        // the screen the menu came over
    Image screen_;            // that, with the windows that closed on it
    Palette palette_;
    std::optional<Menu> menu_;
    int practiceStep_ = 0;    // 1: difficulty menu, 2: skill menu
    std::unique_ptr<Scene> next_;
};

// FINAL main: play_title_pan("title2.pan"), then "if (drv_kbhit())
// play_title_pan("credits.pan")". A key stops an animation (anim_should_abort)
// but stays pending, so a key that stops TITLE2 skips CREDITS too; keys only
// count from the fourth frame (anim_shown < 3: not yet), and mouse clicks are
// swallowed (mouse_clicked: last_key = -1).
class TitleScene : public Scene {
public:
    explicit TitleScene(App& app) : app_(app) {
        // load_picture_swapped("labs.pic", 0): on screen while the title loads.
        auto labs = decodePicture(app.data.read("labs.pic"));
        screen_ = std::move(labs.image);
        palette_ = mcgaPalette(palettes::kGame);
    }

    void handle(const SDL_Event& e) override {
        if (dosKey(e) >= 0) keyPending_ = true;
    }

    void tick() override {
        if (phase_ == Phase::Labs) {
            // The port's pause on the MPS Labs picture (the original shows it
            // while TITLE2 loads); a key ends it, and is still pending for TITLE2.
            if (++labsTicks_ >= kTicksPerSecond * 2 || keyPending_) {
                app_.sound(2);        // FINAL main (start_mode 1): play_sound(2), the title theme
                start("title2.pan", Phase::Title);
            }
            return;
        }
        const bool abort = keyPending_ && player_->framesShown() >= 3;
        if (!abort && !player_->finished()) {
            player_->advance(kTick);
            playSounds();
            if (!player_->finished()) return;
        }
        screen_ = player_->frame();
        if (phase_ == Phase::Title && !keyPending_) start("credits.pan", Phase::Credits);
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
        return std::make_unique<MainMenuScene>(app_, screen_);   // (show_menu flushes the key)
    }

private:
    enum class Phase { Labs, Title, Credits };

    void start(const char* name, Phase p) {
        // Each animation starts from what the screen shows (CREDITS has no
        // background of its own and draws over the end of TITLE2).
        Image from = player_ ? player_->frame() : screen_;
        player_ = std::make_unique<PanPlayer>(PanFile::parse(app_.data.read(name)), PanOptions{}, &from);
        player_->step();
        playSounds();
        phase_ = p;
    }

    // anim_flush_sounds: the PAN's opcode-3 sounds (play_sound, so options & 4).
    void playSounds() {
        for (int n : player_->takeSounds()) app_.sound(n);
    }

    App& app_;
    Image screen_;
    Palette palette_;
    std::unique_ptr<PanPlayer> player_;
    Phase phase_ = Phase::Labs;
    int labsTicks_ = 0;
    bool keyPending_ = false, done_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeTitleScene(App& app) { return std::make_unique<TitleScene>(app); }

std::unique_ptr<Scene> makeMainMenuScene(App& app) {
    Image bg(320, 200, 0);
    return std::make_unique<MainMenuScene>(app, std::move(bg));
}

std::unique_ptr<Scene> makeMainMenuSceneOver(App& app, Image background) {
    return std::make_unique<MainMenuScene>(app, std::move(background));
}

REACTION_SCENE("title", makeTitleScene);
REACTION_SCENE("main-menu", makeMainMenuScene);

}  // namespace reaction
