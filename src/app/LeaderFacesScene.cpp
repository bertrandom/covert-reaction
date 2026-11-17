// The leader line-up (NEWCHAR.C identify_leader_faces): "Max, I'm sure you
// recognize the head of the X among these faces." 15 faces, 5 x 3; arrows (and
// the diagonals Home/PgUp/End/PgDn) move, Enter/Space/Esc choose.
//
// This is the game's copy protection (the leaders' faces are in the manual).
// The 1990 floppy release rejects a wrong face; the GOG and eXoDOS releases have
// the comparison patched out, so any face passes. Covert Reaction follows GOG unless
// app.game.copyProtection is set (COVERT_COPY_PROTECTION=1).
#include <functional>
#include <memory>

#include "app/DosKey.h"
#include "app/FrontEnd.h"
#include "app/Menu.h"
#include "app/NewCharScenes.h"
#include "app/Scenes.h"
#include "app/ScriptScene.h"
#include "formats/Pic.h"
#include "game/Paper.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"
#include "game/Face.h"
#include "game/NewChar.h"

namespace reaction {

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
        next_ = then_(right || !(app_.game.copyProtection || app_.settings.copyProtection), last);
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
        menu_.emplace(app.fonts.font(1), std::move(text), 96, 90, opt).setHotkeys(appHotkeys(app, *this));
    }
    void handle(const SDL_Event& e) override { menu_->handle(e); }
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

// CASE.C case_briefing's end after identify_leader_faces fails, then FINAL.C
// main: "if (scene_vars[0] != 1) { difficulty = 0; sel = training_screen(1L);
// start_practice(sel == -1 ? 0 : sel); }" (ENV[0] = 13 + sel: TAC, CHASE, CODE,
// BUG). The practice program goes back to FINAL, whose main menu follows.
std::unique_ptr<Scene> makeLineupFailedScene(App& app, const Frame& last) {
    std::string text = "I'm afraid you're not quite up to speed yet Max. "
                       "Perhaps you should practice your skills before you take this case.";
    wrapTextBuffer(text, 40);
    auto choice = std::make_shared<int>(-1);
    return std::make_unique<ScriptScene>(app, last, [text, choice](ScriptScene& s) {
        s.insert({
            ScriptScene::storyStep(4),   // set_scene_var(0, 4); set_scene_background("briefing.pan")
            ScriptScene::menuStep(text, 8, 149),
            [](ScriptScene& sc) {        // training_screen(1): training.pic, the practice menu
                const Picture pic = decodePicture(sc.app().data.read("training.pic"));
                sc.screen().blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
            },
            ScriptScene::menuStep(trainingMenu(true), 92, 25, [choice](int r) { *choice = r; }),
            ScriptScene::finishStep([&a = s.app(), choice](const Frame&) {
                a.practiceLevel = 0;     // difficulty = 0
                static const char* scenes[] = {"tac", "chase", "code", "bug"};
                return makeScene(a, scenes[*choice < 0 || *choice > 3 ? 0 : *choice]);
            }),
        });
    });
}

std::unique_ptr<Scene> makeCaseBriefingScene(App& app, const Frame& last, const CaseSetup& setup,
                                             const CaseResult& res, SceneThen startCase) {
    static const char* const kRegions[] = {"Europe.", "Africa/Middle East.", "Central America."};
    std::string region = "We have indications that an operation is in preparation somewhere in ";
    region += kRegions[setup.world >= 0 && setup.world < 3 ? setup.world : 0];
    if (res.briefingOrg >= 0) {
        region += " The ";
        region += app.game.world.orgs[static_cast<std::size_t>(res.briefingOrg)].name.str();
        region += " is known to be involved.";
    }
    wrapTextBuffer(region, 40);
    std::string mission = "Your mission is to prevent this operation from succeeding "
                          "and to capture as many of the participants as possible.";
    if (setup.difficulty)
        mission += " Caution, one or more Double Agents are believed to be active within the CIA.";
    wrapTextBuffer(mission, 40);
    std::string cleared = setup.curMonth == 0 ? "OK, you're cleared. " : "";
    cleared += "Here is some info we've picked up over the last few days. "
               "We think it's linked to this operation...\n";
    wrapTextBuffer(cleared, 40);

    // The chief's "cleared" window over briefing.pan (set_scene_var(0, 1)), then the case.
    auto clearedScene = [&app, cleared, startCase](const Frame& f) -> std::unique_ptr<Scene> {
        return std::make_unique<ScriptScene>(app, f, [cleared, startCase](ScriptScene& s) {
            s.insert({ScriptScene::storyStep(1), ScriptScene::menuStep(cleared, 8, 149),
                      ScriptScene::finishStep(startCase)});
        });
    };
    const bool faceCheck = res.faceCheck;
    const LeaderLineup lineup = res.lineup;
    return std::make_unique<ScriptScene>(app, last, [&app, region, mission, faceCheck, lineup,
                                                     clearedScene](ScriptScene& s) {
        s.insert({
            ScriptScene::menuStep(region, 8, 149, {}, true),   // then clear_text_area()
            ScriptScene::menuStep(mission, 8, 149),
            ScriptScene::finishStep([&app, faceCheck, lineup, clearedScene](const Frame& f) -> std::unique_ptr<Scene> {
                if (!faceCheck) return clearedScene(f);
                return makeLeaderFacesScene(app, lineup, [&app, clearedScene](bool passed, const Frame& f2)
                                                             -> std::unique_ptr<Scene> {
                    if (!passed) return makeLineupFailedScene(app, f2);
                    clearedForDuty(app.game.world);   // options |= 80h
                    app.game.storeCareer();
                    return clearedScene(f2);
                });
            }),
        });
    });
}

REACTION_SCENE("leader-faces", [](App& app) {
    LeaderLineup lineup = drawLeaderLineup(app.game.rng);
    return makeLeaderFacesScene(app, lineup, [&app, lineup](bool passed, const Frame& last) {
        std::string text = passed ? "Cleared for duty.\n" : "Wrong face.\n";
        text = "The leader was at " + std::to_string(lineup.right + 1) + ".\n" + text;
        return std::make_unique<ResultScene>(app, last, text);
    });
});

}  // namespace reaction
