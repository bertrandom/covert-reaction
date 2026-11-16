// The leader line-up (NEWCHAR.C identify_leader_faces): "Max, I'm sure you
// recognize the head of the X among these faces." 15 faces, 5 x 3; arrows (and
// the diagonals Home/PgUp/End/PgDn) move, Enter/Space/Esc choose.
//
// This is the game's copy protection (the leaders' faces are in the manual).
// The 1990 floppy release rejects a wrong face; the GOG and eXoDOS releases have
// the comparison patched out, so any face passes. The redux follows GOG unless
// app.game.copyProtection is set (COVERT_COPY_PROTECTION=1).
#include <functional>
#include <memory>

#include "app/DosKey.h"
#include "app/FrontEnd.h"
#include "app/Menu.h"
#include "app/NewCharScenes.h"
#include "app/Scenes.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"
#include "game/Face.h"
#include "game/NewChar.h"

namespace redux {

namespace {

using FacesThen = std::function<std::unique_ptr<Scene>(bool passed, const Frame& last)>;

class LeaderFacesScene : public Scene {
public:
    LeaderFacesScene(App& app, LeaderLineup lineup, FacesThen then)
        : app_(app), lineup_(lineup), then_(std::move(then)) {
        const FaceSprites faces = FaceSprites::load(app.data);
        screen_.fill(5);
        std::string text = "Max, I'm sure you recognize the head of\nthe ";
        text += kMastermindNames[static_cast<std::size_t>(lineup_.leader)];
        text += " among these faces.\n";
        // dossier_state (paused) = 1: show_menu draws the window and returns.
        TextWindowOptions opt;
        opt.fillColor = 5;
        drawTextWindow(screen_, app.fonts.font(1), text, 16, 1, opt);
        for (int where = 0; where < 15; ++where)
            drawFaceFor(screen_, faces, lineup_.faces[static_cast<std::size_t>(where)], where % 5 * 64 + 4,
                        where / 5 * 60 + 24);
    }

    void handle(const SDL_Event& e) override {
        if (done_) return;
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION) {   // a modern extra
            const SDL_Point p = app_.mouse(e);
            const int cx = p.x / 64, cy = (p.y - 21) / 60;
            if (p.y >= 21 && cx >= 0 && cx < 5 && cy >= 0 && cy < 3) {
                cx_ = cx;
                cy_ = cy;
                if (e.type == SDL_MOUSEBUTTONDOWN) done_ = true;
            }
            return;
        }
        switch (dosKey(e)) {
        case 0x4900: ++cx_; [[fallthrough]];
        case 0x4800: --cy_; break;
        case 0x4D00: ++cx_; break;
        case 0x5100: ++cx_; [[fallthrough]];
        case 0x5000: ++cy_; break;
        case 0x4F00: --cx_; ++cy_; break;
        case 0x4B00: --cx_; break;
        case 0x4700: --cx_; --cy_; break;
        case 13: case 27: case ' ': done_ = true; break;
        default: return;
        }
        cx_ = std::clamp(cx_, 0, 4);
        cy_ = std::clamp(cy_, 0, 2);
    }

    void tick() override {
        if (!done_ || next_) return;
        const int where = cy_ * 5 + cx_;
        const bool right = where == lineup_.right;
        Frame last;
        last.image = Image(320, 200, 5);   // gfx_fill_rect(0, 0, 320, 200, 5); game palette
        last.palette = mcgaPalette(palettes::kGame);
        next_ = then_(right || !app_.game.copyProtection, last);
    }

    void draw(Frame& f) override {
        f.palette = mcgaPalette(palettes::kFace);
        f.image = screen_;
        const int where = cy_ * 5 + cx_;
        // The chosen face's white (13) frame turns black.
        recolorRect(f.image, where % 5 * 64 + 3, where / 5 * 60 + 23, 54, 56, 13, 0);
    }

    std::unique_ptr<Scene> next() override { return std::move(next_); }

private:
    App& app_;
    LeaderLineup lineup_;
    FacesThen then_;
    Image screen_{320, 200, 5};
    int cx_ = 2, cy_ = 1;
    bool done_ = false;
    std::unique_ptr<Scene> next_;
};

// The result, for the "leader-faces" test scene.
class ResultScene : public Scene {
public:
    ResultScene(App& app, Frame back, std::string text) : app_(app), back_(std::move(back)) {
        TextWindowOptions opt;
        opt.showOk = true;
        menu_.emplace(app.fonts.font(1), std::move(text), 96, 90, opt);
    }
    void handle(const SDL_Event& e) override { menu_->handle(e, app_.mouse(e)); }
    void tick() override {
        menu_->tick();
        if (menu_->result() && !next_) next_ = makeMainMenuSceneOver(app_, back_.image);
    }
    void draw(Frame& f) override {
        f = back_;
        menu_->draw(f.image);
    }
    std::unique_ptr<Scene> next() override { return std::move(next_); }

private:
    App& app_;
    Frame back_;
    std::optional<Menu> menu_;
    std::unique_ptr<Scene> next_;
};

}  // namespace

std::unique_ptr<Scene> makeLeaderFacesScene(App& app, LeaderLineup lineup, FacesThen then) {
    return std::make_unique<LeaderFacesScene>(app, lineup, std::move(then));
}

REDUX_SCENE("leader-faces", [](App& app) {
    LeaderLineup lineup = drawLeaderLineup(app.game.rng);
    return makeLeaderFacesScene(app, lineup, [&app, lineup](bool passed, const Frame& last) {
        std::string text = passed ? "Cleared for duty.\n" : "Wrong face.\n";
        text = "The leader was at " + std::to_string(lineup.right + 1) + ".\n" + text;
        return std::make_unique<ResultScene>(app, last, text);
    });
});

}  // namespace redux
