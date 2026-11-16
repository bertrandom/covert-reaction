// The Hall of Fame (FAME.C show_hall_of_fame, hall_of_fame_insert) and the
// vacation snapshot its keys 1-5 show (ENDCASE.C vacation_snapshot).
#include <memory>

#include "app/Dissolve.h"
#include "app/DosKey.h"
#include "app/FrontEnd.h"
#include "app/Menu.h"
#include "app/NewCharScenes.h"
#include "app/Scenes.h"
#include "app/UserFiles.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"
#include "game/Fame.h"
#include "game/Face.h"
#include "game/NewChar.h"
#include "game/Paper.h"

namespace redux {

namespace {

class FameScene : public Scene {
public:
    FameScene(App& app, std::optional<int> insertPoints, SceneThen then)
        : app_(app), then_(std::move(then)), faces_(FaceSprites::load(app.data)), insert_(insertPoints) {
        // load_hall_of_fame: our own copy, else the game directory's FAME.DTA.
        auto gameFile = app.data.find("fame.dta");
        hall_.load(userFile("fame.dta"), gameFile ? *gameFile : std::filesystem::path());
        if (insert_) sel_ = insertEntry();
        drawHall();
    }

    void handle(const SDL_Event& e) override {
        if (next_) return;
        int key = dosKey(e);
        if (key < 0 && e.type == SDL_MOUSEBUTTONDOWN) key = 0;   // a click: like any other key
        if (key < 0) return;
        if (vacation_) {
            if (dissolve_.active() || step_ < static_cast<int>(pics_.frames.size())) keyPending_ = true;
            else endVacation();
            return;
        }
        // show_hall_of_fame's wait_key_or_button.
        if (sel_ != -1 && insert_ && (key == 'c' || key == 'C')) {
            hall_.clear();          // hall_of_fame_insert: start over in an empty Hall
            sel_ = insertEntry();
            drawHall();
            return;
        }
        if (sel_ == -1 && key >= '1' && key <= '5') {
            startVacation(scores_[static_cast<std::size_t>(key - '1')]);
            return;
        }
        finish();
    }

    void tick() override {
        if (!vacation_) return;
        if (dissolve_.active()) {
            dissolve_.tick(screen_);
            return;
        }
        if (step_ < static_cast<int>(pics_.frames.size())) {
            ++step_;
            Image page = composeVacation(step_);
            dissolve_ = Dissolve(std::move(page));
            return;
        }
        if (keyPending_) endVacation();   // keys typed during the pictures are waiting
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        f.image = screen_;
    }

    std::unique_ptr<Scene> next() override { return std::move(next_); }

    // For looking at the layout: sample entries in memory, highlighting `sel`
    // (-1: none), nothing saved.
    void demo(int sel) {
        hall_.clear();
        static const char* names[] = {"Falcon", "Nightshade", "Viper", "Ghost", "Raven", "Sparrow"};
        for (int i = 0; i < HallOfFame::kEntries; ++i)
            hall_.insert(900 - i * 130, i % 4, i % 2 == 1, names[i], i * 3 % 16, i % 3, i * 7);
        sel_ = sel;
        insert_.reset();
        drawHall();
    }

private:
    int insertEntry() {
        const auto& g = app_.game;
        return hall_.insert(*insert_, g.world.hdr.difficulty, (g.world.hdr.options & 2) != 0,
                            g.career.codename.str(), g.career.plotNum, g.career.lastEpisode, g.career.curMonth);
    }

    // show_hall_of_fame's page (one pass of its loop).
    void drawHall() {
        const Font& font = app_.fonts.font(1);
        Image& s = screen_;
        drawPageFrame(s, font, faces_, "Hall of Fame", 1);
        drawTextCentered(s, font, "COVERT ACTION", 160, 16, 0);
        drawTextCentered(s, font, "HALL OF FAME", 160, 24, 0);
        drawLine(s, 80, 32, 240, 32, 14);
        const TextAttr text{1, 0, 15};   // draw_text, opaque on fillcolor 15
        int y = 40;
        for (int i = 0; i < HallOfFame::kEntries; ++i) {
            if (i != sel_ && i == 5) break;
            if (sel_ == 5 && i == 4) continue;
            const int x = app_.game.rng.below(8) + 12;
            drawCardPanel(s, x - 8, y - 4);
            const FameEntry& e = hall_.entries[static_cast<std::size_t>(i)];
            auto& score = scores_[static_cast<std::size_t>(i)];
            score = static_cast<std::uint16_t>(e.score);
            if (e.difficulty & 0x80) score |= 0x8000;
            if (e.score == -1) {
                font.draw(s, x, y, " ---", text);
                y += 26;
                continue;
            }
            const auto lines = fameLines(e, i);
            font.draw(s, x, y, lines[0], {1, 0, 15});
            y += 8;
            font.draw(s, x, y, lines[1], {1, 9, 15});
            y += 8;
            font.draw(s, x, y, lines[2], {1, 7, 15});
            y += 14;
            if (i == sel_) recolorRect(s, x - 4, y - 32, 296, 26, 15, 14);
        }
        if (sel_ != -1) {
            font.draw(s, 252, 189, "C\x83lear", text);
            drawRectOutline(s, 236, 187, 60, 10, 11);
        }
    }

    // vacation_snapshot(clamp((score & 7FFFh) / 150, 0, 20), 0)
    void startVacation(std::uint16_t score) {
        auto& opts = app_.game.world.hdr.options;
        // The original sets the gender bit from the entry and leaves it so.
        opts = static_cast<std::int16_t>(opts & ~2);
        if (score & 0x8000) opts = static_cast<std::int16_t>(opts | 2);
        const Vacation v = vacationFor(std::min((score & 0x7FFF) / 150, 20));
        const bool extra = v.kind <= 1 && app_.game.rng.below(3);
        pics_ = loadVacation(app_.data, v, (opts & 2) != 0, extra);
        screen_ = composeVacation(0);
        if (!pics_.extra.empty()) {
            // The first picture also has laudude1 (it isn't redrawn for the frames).
            screen_ = pics_.back;
            screen_.blit(pics_.extra, 0, 0, 320, 200, 0, 0, 0);
            screen_.blit(pics_.hand, 0, 0, 320, 200, 0, 0, 0);
        }
        step_ = 0;
        keyPending_ = false;
        vacation_ = true;
    }

    // icon_page: the background, frames 1..k, the hand on top.
    Image composeVacation(int k) const {
        Image page = pics_.back;
        for (int m = 0; m < k; ++m) page.blit(pics_.frames[static_cast<std::size_t>(m)], 0, 0, 320, 200, 0, 0, 0);
        page.blit(pics_.hand, 0, 0, 320, 200, 0, 0, 0);
        return page;
    }

    void endVacation() {
        vacation_ = false;
        pics_ = {};
        drawHall();   // play_sound(1); the loop draws the Hall again
    }

    void finish() {
        if (next_) return;
        if (insert_) hall_.save(userFile("fame.dta"));   // save_hall_of_fame
        Frame last;
        last.image = screen_;
        last.palette = palette_;
        next_ = then_(last);
    }

    App& app_;
    SceneThen then_;
    FaceSprites faces_;
    HallOfFame hall_;
    std::optional<int> insert_;
    int sel_ = -1;
    std::array<std::uint16_t, HallOfFame::kEntries> scores_{};
    Palette palette_ = mcgaPalette(palettes::kGame);
    Image screen_{320, 200, 0};
    bool vacation_ = false, keyPending_ = false;
    VacationPictures pics_;
    int step_ = 0;
    Dissolve dissolve_;
    std::unique_ptr<Scene> next_;
};

std::unique_ptr<Scene> backToMenu(App& app, const Frame& last) { return makeMainMenuSceneOver(app, last.image); }

}  // namespace

std::unique_ptr<Scene> makeHallOfFameScene(App& app, std::optional<int> insertPoints, SceneThen then) {
    return std::make_unique<FameScene>(app, insertPoints, std::move(then));
}

REDUX_SCENE("hall-of-fame", [](App& app) {
    return makeHallOfFameScene(app, std::nullopt, [&app](const Frame& last) { return backToMenu(app, last); });
});

REDUX_SCENE("fame-demo", [](App& app) -> std::unique_ptr<Scene> {
    auto s = std::make_unique<FameScene>(app, std::nullopt, [&app](const Frame& last) { return backToMenu(app, last); });
    s->demo(-1);
    return s;
});

REDUX_SCENE("fame-demo-insert", [](App& app) -> std::unique_ptr<Scene> {
    auto s = std::make_unique<FameScene>(app, std::nullopt, [&app](const Frame& last) { return backToMenu(app, last); });
    s->demo(2);
    return s;
});

}  // namespace redux
