// FINAL at the end of a case (covert-decomp FINAL/ENDCASE.C end_of_case) and the
// way its main goes on to the next case (FINAL.C "resume": plot_briefing,
// case_intro_story, generate_case). The logic is src/game/EndCase.*; these are
// the original's screens in its order, built on ScriptScene.
#include <memory>

#include "app/CityLoop.h"
#include "app/EndCase.h"
#include "app/FrontEnd.h"
#include "app/LoadGame.h"
#include "app/NewCharScenes.h"
#include "app/Scenes.h"
#include "app/ScriptScene.h"
#include "formats/TextDb.h"
#include "game/CaseGen.h"
#include "game/EndCase.h"
#include "game/Fame.h"
#include "game/NewChar.h"
#include "game/Paper.h"

namespace redux {

namespace {

// play_sound(n) as a step (FINAL: when options & 4).
ScriptScene::Step soundStep(int n) {
    return [n](ScriptScene& s) { s.app().sound(n); };
}

using Step = ScriptScene::Step;
using EcPtr = std::shared_ptr<EndOfCase>;

std::unique_ptr<Scene> mainMenu(App& app, const Frame& last) { return makeMainMenuSceneOver(app, last.image); }

// The story pages of plot.txt `key`, each a window at (8, 149) cleared after.
std::vector<Step> storySteps(const TextDb& plot, const std::string& key) {
    std::vector<Step> out;
    for (auto& page : storyPages(plot, key)) out.push_back(ScriptScene::menuStep(page, 8, 149, {}, true));
    return out;
}

// ---------------------------------------------------------------- the next case

// GAME's start of a new case (ENV[1] = 0: mission_briefing, then the city loop).
std::unique_ptr<Scene> startCityForNewCase(App& app, const Frame& last) { return makeCityNewCaseScene(app, last); }

// FINAL main after case_intro_story: generate_case (with case_briefing's line-up
// when it asks for one), save_env, then GAME. A wrong face sends the original to
// a practice mission instead.
std::unique_ptr<Scene> generateCase(App& app, const Frame& last) {
    auto& g = app.game;
    CaseResult res = CaseGenerator(g.world, g.rng, app.data, caseSetupFor(g)).generate();
    g.storeCareer();
    if (!res.faceCheck) return startCityForNewCase(app, last);
    return makeLeaderFacesScene(app, res.lineup, [&app](bool passed, const Frame& f) -> std::unique_ptr<Scene> {
        if (passed) {
            clearedForDuty(app.game.world);
            app.game.storeCareer();
            return startCityForNewCase(app, f);
        }
        return makeLineupFailedScene(app, f);
    });
}

// plot_briefing: a new plot, its MasterMind and world, the briefing pages.
Step plotBriefingStep(std::shared_ptr<TextDb> plot) {
    return [plot](ScriptScene& s) {
        auto& g = s.app().game;
        s.screen().fill(5);   // gfx_fill_rect(g_gfx, 0, 0, 320, 200, 5)
        const std::string key = choosePlot(g, s.app().data);
        std::vector<Step> steps{ScriptScene::storyStep(4)};
        for (auto& st : storySteps(*plot, key)) steps.push_back(std::move(st));
        steps.push_back([](ScriptScene& s2) { s2.app().game.briefed = true; });
        s.insert(std::move(steps));
    };
}

// case_intro_story: the episode (a new plot when none is left), its story when
// the plot wasn't just briefed, the clock reset; then generate_case.
Step caseIntroStep(std::shared_ptr<TextDb> plot) {
    return [plot](ScriptScene& s) {
        auto& g = s.app().game;
        EpisodeChoice ep = chooseEpisode(g);
        if (!ep.ok) {
            s.insert({plotBriefingStep(plot), caseIntroStep(plot)});
            return;
        }
        std::vector<Step> steps;
        if (ep.showStory) {
            steps.push_back(ScriptScene::storyStep(4));
            for (auto& st : storySteps(*plot, ep.storyKey)) steps.push_back(std::move(st));
        }
        steps.push_back([](ScriptScene& s2) {
            startCaseClock(s2.app().game);
            s2.app().game.storeCareer();
        });
        App& app = s.app();
        steps.push_back(ScriptScene::finishStep([&app](const Frame& f) { return generateCase(app, f); }));
        s.insert(std::move(steps));
    };
}

// ---------------------------------------------------------------- end_of_case

void continueMenu(ScriptScene& s, const EcPtr& ec);

std::unique_ptr<Scene> endGame(App& app, const Frame& last) {
    // set_exit_state(); ENV[0] = 0xFF; exit(): COVERT.EXE ends. The redux goes
    // back to the main menu.
    app.game.storeCareer();
    return mainMenu(app, last);
}

void continueGame(ScriptScene& s, const EcPtr& ec) {
    ec->game().storeCareer();
    App& app = s.app();
    // set_scene_var(0, 4); set_scene_background("briefing.pan"), then FINAL's main.
    s.insert({ScriptScene::finishStep([&app](const Frame& f) { return makeNextCaseScene(app, f); })});
}

void continueMenu(ScriptScene& s, const EcPtr& ec) {
    App& app = s.app();
    s.insert({ScriptScene::menuStep(ec->continueMenu(), 80, 80, [&s, ec, &app](int choice) {
        if (choice == 3) {   // Change Difficulty Level
            s.insert({ScriptScene::menuStep(EndOfCase::difficultyMenu(), 80, 80, [&s, ec](int r) {
                ec->changeDifficulty(r);
                continueMenu(s, ec);
            })});
            return;
        }
        if (choice == 2) {
            s.insert({ScriptScene::finishStep([&app](const Frame& f) { return endGame(app, f); })});
            return;
        }
        if (choice == 1) {   // Save Game, then "Continue Game / End Game"
            ec->beforeSave();
            s.insert({ScriptScene::finishStep([&app, ec](const Frame& f) {
                return makeSaveGameScene(app, 0, f, [&app, ec](const Frame& f2) -> std::unique_ptr<Scene> {
                    return std::make_unique<ScriptScene>(app, f2, [ec, &app](ScriptScene& s2) {
                        s2.insert({ScriptScene::menuStep(EndOfCase::afterSaveMenu(), 80, 80, [&s2, ec, &app](int r) {
                            if (r == 1)
                                s2.insert({ScriptScene::finishStep([&app](const Frame& f3) { return endGame(app, f3); })});
                            else
                                continueGame(s2, ec);
                        })});
                    });
                });
            })});
            return;
        }
        continueGame(s, ec);
    })});
}

// After the Hall of Fame: the case in the history, the career, the promotion,
// the retirement or the menu.
std::unique_ptr<Scene> careerPart(App& app, const EcPtr& ec, const Frame& last) {
    return std::make_unique<ScriptScene>(app, last, [ec](ScriptScene& s) {
        ec->recordCase();
        s.insert({
            ScriptScene::pagesStep(ec->careerSummary()),
            [ec](ScriptScene& s2) {
                if (auto t = ec->promotion())
                    s2.insert({ScriptScene::storyStep(1), ScriptScene::menuStep(*t, 8, 149, {}, true)});
            },
            [ec](ScriptScene& s2) {
                s2.disarm();   // anim_active = 0
                ec->game().storeCareer();
                if (auto t = ec->retirement()) {
                    App& app2 = s2.app();
                    // rogues_gallery: play_sound(2) ... show_menu; play_sound(1)
                    s2.insert({soundStep(2), ScriptScene::pagesStep(ec->roguesGallery()),
                               ScriptScene::menuStep(*t, ec->rogueMenuX(), 0x40), soundStep(1),
                               ScriptScene::finishStep([&app2](const Frame& f) { return endGame(app2, f); })});
                    return;
                }
                continueMenu(s2, ec);
            },
        });
    });
}

// end_of_case up to the Hall of Fame.
std::unique_ptr<Scene> debriefing(App& app) {
    EndOfCase::startFinal(app.game);
    auto ec = std::make_shared<EndOfCase>(app.game, app.data);
    auto plot = std::make_shared<TextDb>(TextDb::load(app.data, "plot.txt"));
    Frame start;
    start.image = Image(320, 200, 0);
    return std::make_unique<ScriptScene>(app, start, [ec, plot](ScriptScene& s) {
        App& app2 = s.app();
        s.insert({
            soundStep(2),   // end_of_case: play_sound(2)
            ScriptScene::storyStep(4),
            ScriptScene::menuStep(EndOfCase::introText(), 8, 149, {}, true),
            // case_timeline
            [ec](ScriptScene& s2) {
                s2.screen().fill(1);
                ec->begin();
                auto tl = std::make_shared<std::vector<Page>>(ec->timeline());
                s2.insert({ScriptScene::pagesStep(*tl, [ec, tl](std::size_t i, bool esc) {
                    if (esc) ec->timelineEscaped((*tl)[i]);
                })});
            },
            // ep = efficiency_report(); play_sound(1)
            [ec](ScriptScene& s2) { s2.insert({ScriptScene::pagesStep(ec->efficiencyReport()), soundStep(1)}); },
            // the plot's story of the episode
            [ec, plot](ScriptScene& s2) {
                std::vector<Step> steps{ScriptScene::storyStep(ec->storyVar())};
                for (auto& st : storySteps(*plot, ec->storyKey())) steps.push_back(std::move(st));
                s2.insert(std::move(steps));
            },
            // the interrogation's clue about Mr. Big
            [ec](ScriptScene& s2) {
                auto t = ec->afterStory();
                if (!t) return;
                s2.insert({ScriptScene::menuStep(*t, 8, 149), [ec](ScriptScene& s3) {
                               std::vector<Step> steps;
                               for (auto& c : ec->mastermindClue()) {
                                   steps.push_back(ScriptScene::pagesStep(std::move(c.dossier)));
                                   if (c.notice) steps.push_back(ScriptScene::menuStep(*c.notice, 20, 60));
                               }
                               s3.insert(std::move(steps));
                           }});
            },
            [ec](ScriptScene& s2) {
                auto t = ec->capturedText();
                if (!t) return;
                fillRect(s2.screen(), 4, 149, 312, 48, 5);   // clear_text_area
                s2.insert({ScriptScene::menuStep(*t, 8, 149)});
            },
            // vacation_snapshot(n, 1)
            [ec](ScriptScene& s2) {
                const int n = ec->vacationIndex();
                std::string caption = vacationCaption(vacationFor(n).kind);
                wrapTextBuffer(caption, 24);
                s2.screen().fill(0);
                s2.insert({ScriptScene::pausedStep(caption, 0x40, 0x50), ScriptScene::retraceStep(120),
                           ScriptScene::vacationStep(n, [ec](int kind) { return ec->vacationExtra(kind); }),
                           ScriptScene::anyKeyStep(), soundStep(1)});
            },
            // load_hall_of_fame; hall_of_fame_insert(ep); save_hall_of_fame
            ScriptScene::finishStep([&app2, ec](const Frame&) {
                return makeHallOfFameScene(app2, ec->ep(),
                                           [&app2, ec](const Frame& f) { return careerPart(app2, ec, f); });
            }),
        });
    });
}

}  // namespace

std::unique_ptr<Scene> makeEndOfCaseScene(App& app) { return debriefing(app); }

std::unique_ptr<Scene> makeNextCaseScene(App& app, const Frame& last) {
    auto plot = std::make_shared<TextDb>(TextDb::load(app.data, "plot.txt"));
    return std::make_unique<ScriptScene>(app, last, [plot](ScriptScene& s) {
        const auto& g = s.app().game;
        std::vector<Step> steps;
        if (g.world.persons[0].fate == -1 || g.career.plotNum == 0) steps.push_back(plotBriefingStep(plot));
        steps.push_back(caseIntroStep(plot));
        s.insert(std::move(steps));
    });
}

// The debriefing on its own: a case played by the simulation from a fixed seed
// (COVERT_SEED, else 1990), then end_of_case.
REDUX_SCENE("endcase", [](App& app) -> std::unique_ptr<Scene> {
    std::uint32_t seed = 1990;
    if (const char* s = std::getenv("COVERT_SEED")) seed = static_cast<std::uint32_t>(std::strtoul(s, nullptr, 0));
    makeDemoFinishedCase(app.game, app.data, seed);
    return makeEndOfCaseScene(app);
});

// The same at the end of a ten-year career, with five MasterMinds arrested
// before: the career's faces, the retirement and the rogues' gallery.
REDUX_SCENE("endcase-retire", [](App& app) -> std::unique_ptr<Scene> {
    makeDemoFinishedCase(app.game, app.data, 1990);
    app.game.career.curMonth = 119;
    app.game.career.mastermindsArrested = 0x0210A5;
    app.game.storeCareer();
    return makeEndOfCaseScene(app);
});

// The next case's briefing on its own (as after "Continue Game").
REDUX_SCENE("next-case", [](App& app) -> std::unique_ptr<Scene> {
    makeDemoFinishedCase(app.game, app.data, 1990);
    Frame f;
    f.image = Image(320, 200, 5);
    return makeNextCaseScene(app, f);
});

}  // namespace redux
