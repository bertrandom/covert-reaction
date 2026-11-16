// A new character (NEWCHAR.C create_new_character, codename_dialog,
// enter_codename, training_screen), the plot briefing (plot_briefing) and the
// episode story (case_intro_story), as FINAL's main runs them after "Create a
// New Character": gender, code name, difficulty, training, plot briefing, case.
// The logic is src/game/NewChar.*; these scenes draw it and take the keys.
#include <cstdio>
#include <memory>

#include "app/CityLoop.h"
#include "app/DosKey.h"
#include "app/FrontEnd.h"
#include "app/Menu.h"
#include "app/NewCharScenes.h"
#include "app/Scenes.h"
#include "formats/McgaPalette.h"
#include "formats/Pan.h"
#include "formats/Pic.h"
#include "formats/TextDb.h"
#include "game/CaseGen.h"
#include "game/NewChar.h"
#include "game/Paper.h"

namespace redux {

namespace {

constexpr double kTick = 1.0 / kTicksPerSecond;

Image loadScreen(const GameData& data, const char* name) {
    // load_picture_swapped(name, 1) + copy of the back page to the screen.
    Image img(320, 200, 0);
    auto pic = decodePicture(data.read(name));
    img.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
    return img;
}

TextWindowOptions fill5(int selected = 0) {
    TextWindowOptions o;
    o.fillColor = 5;     // g_gfx->fillcolor: odd y windows are filled with it
    o.selected = selected;
    return o;
}

// The plot briefing and the episode (plot_briefing, case_intro_story), then
// `then` (generate_case's place).
class BriefingScene : public Scene {
public:
    BriefingScene(App& app, SceneThen then)
        : app_(app), then_(std::move(then)), plotText_(TextDb::load(app.data, "plot.txt")) {
        plotBriefing();
    }

    void handle(const SDL_Event& e) override {
        if (menu_) menu_->handle(e, app_.mouse(e));
    }

    void tick() override {
        if (anim_ && animating_) anim_->advance(kTick);
        if (!menu_) return;
        menu_->tick();
        if (!menu_->result()) return;
        // text_window ends the animation (anim_stop): its last frame stays.
        if (anim_ && animating_) screen_ = anim_->frame();
        animating_ = false;
        fillRect(screen_, 4, 149, 312, 48, 5);  // clear_text_area
        menu_.reset();
        ++page_;
        nextPage();
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        if (anim_ && animating_) {
            f.image = anim_->frame();
            // draw_text_window with the animation on, y >= 144: the strip below.
            fillRect(f.image, 4, 149, 312, 198 - 149, 5);
        } else {
            f.image = screen_;
        }
        if (menu_) menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override { return std::move(next_); }

private:
    enum class Story { Plot, Episode };

    void plotBriefing() {
        screen_ = Image(320, 200, 5);   // gfx_fill_rect(0, 0, 320, 200, 5)
        const std::string key = choosePlot(app_.game, app_.data);
        startStory(storyPages(plotText_, key), Story::Plot);
    }

    // set_scene_var(0, 4); set_scene_background("briefing.pan"); then the pages.
    void startStory(std::vector<std::string> pages, Story s) {
        story_ = s;
        pages_ = std::move(pages);
        page_ = 0;
        anim_.reset();
        animating_ = false;
        if (app_.game.world.hdr.options & 8) {
            if (!pages_.empty()) {
                // text_window starts the animation from the screen as it is.
                anim_ = std::make_unique<PanPlayer>(PanFile::parse(app_.data.read("briefing.pan")), PanOptions{},
                                                    &screen_);
                anim_->setVar(0, 4);
                anim_->step();
                animating_ = true;
            }
        } else {
            screen_ = Image(320, 200, 5);
        }
        nextPage();
    }

    void nextPage() {
        if (page_ < static_cast<int>(pages_.size())) {
            menu_.emplace(app_.fonts.font(1), pages_[static_cast<std::size_t>(page_)], 8, 149, fill5());
            return;
        }
        if (story_ == Story::Plot) app_.game.briefed = true;
        else {
            finish();
            return;
        }
        // case_intro_story
        EpisodeChoice ep = chooseEpisode(app_.game);
        if (!ep.ok) {
            plotBriefing();   // no episode left: brief a new plot and try again
            return;
        }
        if (ep.showStory) startStory(storyPages(plotText_, ep.storyKey), Story::Episode);
        else finish();
    }

    void finish() {
        startCaseClock(app_.game);
        app_.game.storeCareer();
        const auto& g = app_.game;
        const auto& c = g.career;
        const auto& org = g.world.orgs[static_cast<std::size_t>(c.mm.org)];
        std::fprintf(stderr,
                     "new case: codename '%s' (%s), difficulty %d, skills %03X; plot %d \"%s\", episode %d, "
                     "crime %d (%s), world %d; MasterMind org %d %s (leader %d %s), city %d %s, id %04X; "
                     "roles \"%s\" / \"%s\"\n",
                     c.codename.str().c_str(), (g.world.hdr.options & 2) ? "Maxine" : "Max", g.world.hdr.difficulty,
                     static_cast<unsigned>(g.world.hdr.skillLevels) & 0xFFFF, c.plotNum,
                     kPlotDefs[static_cast<std::size_t>(c.plotNum)].name, c.lastEpisode, c.episodeCrime,
                     c.episodeCrime >= 0 && c.episodeCrime < 13 ? kCrimeNames[static_cast<std::size_t>(c.episodeCrime)]
                                                                : "?",
                     c.worldNum, c.mm.org, org.name.str().c_str(), c.leaderCode,
                     c.leaderCode >= 0 && c.leaderCode < 26 ? kMastermindNames[static_cast<std::size_t>(c.leaderCode)]
                                                            : "?",
                     c.mm.city, g.world.cities[static_cast<std::size_t>(c.mm.city)].name.str().c_str(),
                     static_cast<unsigned>(c.mm.id) & 0xFFFF, c.playerName.str().c_str(),
                     c.playerOrgName.str().c_str());
        Frame last;
        last.image = screen_;
        last.palette = palette_;
        next_ = then_(last);
    }

    App& app_;
    SceneThen then_;
    TextDb plotText_;
    Palette palette_ = mcgaPalette(palettes::kGame);
    Image screen_{320, 200, 5};
    std::unique_ptr<PanPlayer> anim_;
    bool animating_ = false;
    std::vector<std::string> pages_;
    int page_ = 0;
    Story story_ = Story::Plot;
    std::optional<Menu> menu_;
    std::unique_ptr<Scene> next_;
};

// create_new_character: gender, code name, difficulty, then training_screen(0).
class NewCharacterScene : public Scene {
public:
    NewCharacterScene(App& app, SceneThen then) : app_(app), then_(std::move(then)) {
        startNewCharacter(app_.game);
        screen_ = loadScreen(app_.data, "gender.pic");
        menu_.emplace(app_.fonts.font(1), kGenderMenu, 96, 33, fill5());
    }

    void handle(const SDL_Event& e) override {
        if (phase_ == Phase::Codename) {
            int k = textChar(e);
            if (k < 0 && !keyComesAsText(e)) k = dosKey(e);
            if (k >= 0) codenameKey(k);
            return;
        }
        if (menu_) menu_->handle(e, app_.mouse(e));
    }

    void tick() override {
        if (phase_ == Phase::Codename) {
            blink_ = (blink_ + 1) % 20;
            return;
        }
        if (phase_ == Phase::Done) {
            // wait_retrace(60) after the final ratings.
            if (--wait_ <= 0 && !next_) next_ = std::make_unique<BriefingScene>(app_, then_);
            return;
        }
        if (!menu_) return;
        menu_->tick();
        auto r = menu_->result();
        if (!r) return;
        menu_.reset();
        switch (phase_) {
        case Phase::Gender:
            chooseGender(app_.game, *r);
            startCodename();
            break;
        case Phase::Difficulty:
            chooseDifficulty(app_.game, *r);
            startTraining();
            break;
        case Phase::Training:
            trainingChoice(*r);
            break;
        default:
            break;
        }
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        f.image = screen_;
        if (phase_ == Phase::Codename && blink_ < 10)  // the cursor blinks while no key is down
            recolorRect(f.image, cursorX_, kFieldY + 1, cursorW_, 10, 5, 2);
        if (menu_) menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override { return std::move(next_); }

private:
    enum class Phase { Gender, Codename, Difficulty, Training, Done };
    static constexpr int kFieldX = 106, kFieldY = 100, kFieldLen = 15;

    // codename_dialog
    void startCodename() {
        phase_ = Phase::Codename;
        Image& s = screen_;
        fillRectXY(s, 99, 85, 221, 114, 8);
        fillRectXY(s, 100, 87, 220, 113, 5);
        fillRectXY(s, 101, 88, 219, 112, 8);
        fillRectXY(s, 105, 101, 215, 110, 5);
        drawLine(s, 101, 86, 219, 86, 7);
        drawLine(s, 103, 99, 217, 99, 5);
        drawLine(s, 102, 112, 218, 112, 5);
        drawLine(s, 104, 101, 104, 110, 7);
        drawLine(s, 216, 101, 216, 110, 7);
        drawTextCentered(s, app_.fonts.font(1), "Max's code name is:", 161, 90, 2);
        // enter_codename(106, 100, 15)
        fillRect(s, kFieldX, kFieldY + 1, kFieldLen * 7, 9, 5);
        field_ = CodenameField(kFieldLen);
        drawField();
        blink_ = 0;
    }

    // draw_codename_field
    void drawField() {
        const Font& font = app_.fonts.font(1);
        int x = kFieldX + 2;
        const int y = kFieldY + 2;
        const std::string& t = field_.buffer();
        for (int i = 0; i < kFieldLen; ++i) {
            fillRect(screen_, x, y, 8, 8, 5);
            const auto ch = static_cast<std::uint8_t>(t[static_cast<std::size_t>(i)]);
            font.draw(screen_, x, y, std::string(1, static_cast<char>(ch)), {1, 10, 5});
            if (i == field_.cursor()) {
                cursorX_ = x;
                cursorW_ = font.charWidth(ch);
            }
            x += font.charWidth(ch);
        }
    }

    void codenameKey(int k) {
        blink_ = 0;
        if (field_.key(k)) {
            app_.game.career.codename.set(field_.result());
            phase_ = Phase::Difficulty;
            menu_.emplace(app_.fonts.font(1), kDifficultyMenu, 96, 129, fill5());
            return;
        }
        drawField();
    }

    // training_screen(0)
    void startTraining() {
        phase_ = Phase::Training;
        screen_ = loadScreen(app_.data, "training.pic");
        lvl_ = {0, 0, 0, 0};
        total_ = 0;
        defaultSel_ = 0;
        openTrainingMenu();
    }

    void openTrainingMenu() {
        drawRatings(lvl_);
        menu_.emplace(app_.fonts.font(1), trainingMenu(false), 92, 25, fill5(defaultSel_));
    }

    void trainingChoice(int choice) {
        defaultSel_ = choice;
        if (choice != -1 && lvl_[static_cast<std::size_t>(choice)] != 3) {
            ++total_;
            ++lvl_[static_cast<std::size_t>(choice)];
        }
        if (choice != -1 && total_ != 4) {
            openTrainingMenu();
            return;
        }
        std::array<int, 4> shown{};
        finishTraining(app_.game, lvl_, total_, &shown);
        drawRatings(shown);
        phase_ = Phase::Done;
        wait_ = 60;
    }

    // training_rating
    void drawRatings(const std::array<int, 4>& lvl) {
        for (int i = 0; i < 4; ++i) {
            fillRect(screen_, i * 80 + 8, 188, 64, 8, kRatingColors[static_cast<std::size_t>(i)]);
            drawTextCentered(screen_, app_.fonts.font(1), ratingName(lvl[static_cast<std::size_t>(i)]), i * 80 + 40,
                             188, 14);
        }
    }

    App& app_;
    SceneThen then_;
    Palette palette_ = mcgaPalette(palettes::kGame);
    Image screen_;
    std::optional<Menu> menu_;
    Phase phase_ = Phase::Gender;
    CodenameField field_;
    int blink_ = 0, cursorX_ = kFieldX + 2, cursorW_ = 0;
    std::array<int, 4> lvl_{};
    int total_ = 0, defaultSel_ = 0, wait_ = 0;
    std::unique_ptr<Scene> next_;
};

// Until case generation and the briefing are wired in: say so over the last
// screen, then the main menu.
std::unique_ptr<Scene> caseNotPorted(App& app, const Frame& last);

// The end of FINAL's start-up: generate_case (with case_briefing's leader line-up
// when it asks for one), then GAME: the mission briefing and the city loop.
std::unique_ptr<Scene> caseNotPorted(App& app, const Frame& last) {
    auto& g = app.game;
    CaseResult res = CaseGenerator(g.world, g.rng, app.data, caseSetupFor(g)).generate();
    g.storeCareer();
    auto startNotice = [&app](const Frame& f) -> std::unique_ptr<Scene> {
        app.game.storeCareer();
        return makeCityNewCaseScene(app, f);
    };
    if (!res.faceCheck) return startNotice(last);
    return makeLeaderFacesScene(app, res.lineup, [&app, startNotice](bool passed, const Frame& f) -> std::unique_ptr<Scene> {
        if (passed) {
            clearedForDuty(app.game.world);
            return startNotice(f);
        }
        // FINAL drops the case and sends a wrong answer to a practice mission.
        return makeLineupFailedScene(app, f);
    });
}

}  // namespace

std::unique_ptr<Scene> makeNewCharacterScene(App& app, SceneThen then) {
    return std::make_unique<NewCharacterScene>(app, std::move(then));
}

REDUX_SCENE("new-character", [](App& app) {
    return makeNewCharacterScene(app, [&app](const Frame& last) { return caseNotPorted(app, last); });
});

// The plot briefing on its own (difficulty and career as they are: a first case
// on Local Disturbance gets "Welcome Max").
REDUX_SCENE("plot-briefing", [](App& app) -> std::unique_ptr<Scene> {
    return std::make_unique<BriefingScene>(app, [&app](const Frame& last) { return caseNotPorted(app, last); });
});

}  // namespace redux
