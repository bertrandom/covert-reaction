// The city loop scene (GAME.EXE between the other programs) and the hand-over to
// the mini-games and the debriefing. See CityLoop.h.
#include "app/CityLoop.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "app/BugScene.h"
#include "app/CityGame.h"
#include "app/EndCase.h"
#include "app/GameThread.h"
#include "app/MiniGames.h"
#include "app/Scenes.h"
#include "game/CaseGen.h"
#include "game/NewChar.h"
#include "game/TacCase.h"
#include "game/Wiretap.h"

namespace redux {

namespace {

std::uint32_t gameSeed() {
    if (const char* v = std::getenv("COVERT_GAME_SEED"); v && *v)
        return static_cast<std::uint32_t>(std::strtoul(v, nullptr, 0)) & 0x7FFF;
    return static_cast<std::uint32_t>(std::time(nullptr)) & 0x7FFF;   // randomize()
}

std::unique_ptr<Scene> afterGame(App& app, std::shared_ptr<CityEnv> env, int cityExit);

class CityLoopScene : public Scene {
public:
    CityLoopScene(App& app, std::shared_ptr<CityEnv> env, std::uint32_t seed)
        : app_(app), env_(std::move(env)), thread_([this] { body(); }), ui_(app, thread_),
          game_(app, *env_, ui_, seed) {}

    ~CityLoopScene() override = default;

    void handle(const SDL_Event& e) override { ui_.handle(e); }

    void tick() override {
        if (!started_) {
            started_ = true;
            thread_.resume();
        } else if (!thread_.done() && ui_.tick()) {
            thread_.resume();
        }
        if (ui_.childQuit()) quit = true;
    }

    void draw(Frame& f) override { ui_.draw(f); }

    std::unique_ptr<Scene> next() override {
        if (!thread_.done() || left_) return nullptr;
        left_ = true;
        if (caseOver_) return makeEndOfCaseScene(app_);
        if (exit_ == -1) {
            quit = true;
            return nullptr;
        }
        return afterGame(app_, env_, exit_);
    }

private:
    void body() {
        try {
            exit_ = game_.run();
        } catch (const CaseOverExit&) {
            caseOver_ = true;
            exit_ = 9;
            (*env_)[0] = 9;
        }
        std::fprintf(stderr, "city loop: exit %d\n", exit_);
    }

    App& app_;
    std::shared_ptr<CityEnv> env_;
    GameThread thread_;
    CityUi ui_;
    CityGame game_;
    bool started_ = false, left_ = false, caseOver_ = false;
    int exit_ = 0;
};

// Back to GAME after a mini-game (ENV[0] = 0).
std::unique_ptr<Scene> resumeGame(App& app, std::shared_ptr<CityEnv> env) {
    (*env)[0] = 0;
    return std::make_unique<CityLoopScene>(app, std::move(env), gameSeed());
}

std::unique_ptr<Scene> startTac(App& app, std::shared_ptr<CityEnv> env, int program) {
    auto& g = app.game;
    TacSetup s = TacCaseHooks::setupFrom(g.world, program, static_cast<std::int8_t>((*env)[12]), gameSeed());
    // GAMELIB's routines inside the building work on the case's tables (game/TacCase.h)
    auto hooks = std::make_shared<TacCaseHooks>(g.world, app.data, g.career);
    return makeTacScene(app, std::move(s), hooks, [env, hooks](App& a, const TacResult& r) {
        (*env)[8] = static_cast<std::uint8_t>(r.score);
        (*env)[9] = static_cast<std::uint8_t>(r.violence);
        (*env)[10] = static_cast<std::uint8_t>(r.missionFlags);
        (*env)[11] = static_cast<std::uint8_t>(r.wounds);
        (*env)[13] = static_cast<std::uint8_t>(r.env13);
        (*env)[15] = static_cast<std::uint8_t>(r.minutes);
        TacCaseHooks::applyResult(a.game.world, r);
        for (const auto& line : hooks->trace) std::fprintf(stderr, "tac lib: %s\n", line.c_str());
        return resumeGame(a, env);
    });
}

std::unique_ptr<Scene> startChase(App& app, std::shared_ptr<CityEnv> env, int program) {
    const auto& w = app.game.world;
    const auto& h = w.hdr;
    ChaseSetup s;
    s.mode = program == 6 ? 2 : program == 5 ? 1 : 0;
    s.sex = (*env)[10] & 1;
    s.cityId = h.hereCity & 15;
    s.targetBuilding = h.hereOrg;
    s.startBuilding = static_cast<std::int8_t>((*env)[9]);
    s.options = static_cast<std::uint16_t>(h.options & ~0x20);
    s.gameTime = static_cast<std::uint16_t>(h.clockBase);
    s.difficulty = h.difficulty;
    s.envFlags = static_cast<std::uint16_t>(h.skillLevels);
    s.alertLevel = h.var48;
    s.orgMask = w.cities[static_cast<std::size_t>(s.cityId)].orgsKnown;
    for (std::size_t i = 0; i < 16; ++i) s.orgAbbrev[i] = w.orgs[i].abbrev.str();
    s.seed = gameSeed();
    return makeChaseScene(app, s, [env](App& a, const ChaseResult& r) {
        auto& hh = a.game.world.hdr;
        (*env)[9] = static_cast<std::uint8_t>(r.outcome);
        (*env)[10] = static_cast<std::uint8_t>(r.score);
        (*env)[13] = 1;
        if (r.buildingReached >= 0) {
            (*env)[14] = static_cast<std::uint8_t>(r.buildingReached);
            hh.hereOrg = static_cast<std::int16_t>(r.buildingReached);
        }
        (*env)[15] = static_cast<std::uint8_t>(r.minutes);
        hh.skillLevels = static_cast<std::int16_t>(r.envFlags);
        hh.options = static_cast<std::int16_t>(r.options);
        return resumeGame(a, env);
    });
}

std::unique_ptr<Scene> startBug(App& app, std::shared_ptr<CityEnv> env, int program) {
    WiretapSetup s = WiretapSetup::fromEnv(app.game.world.hdr, program);
    s.practice = false;
    s.seed = gameSeed();
    auto hooks = std::make_shared<BugCaseHooks>(app.game.world, app.data, app.game.career);
    return makeBugScene(app, s, [env, hooks](App& a, const WiretapResult& r) -> std::unique_ptr<Scene> {
        for (const auto& line : hooks->trace) std::fprintf(stderr, "bug lib: %s\n", line.c_str());
        (*env)[8] = r.envProgress();
        (*env)[9] = r.envAlarms();
        (*env)[15] = 0;
        return resumeGame(a, env);
    }, hooks);
}

std::unique_ptr<Scene> startCode(App& app, std::shared_ptr<CityEnv> env) {
    const auto& h = app.game.world.hdr;
    CodeSetup s;
    s.message = env->intercept;
    s.gameDifficulty = h.difficulty;
    s.skillLevels = h.skillLevels;
    s.options = static_cast<unsigned>(h.options & ~0x20) & 0xFFFF;
    s.videoMode = h.videoMode;
    s.seed = gameSeed();
    return makeCodeScene(app, s, [env](App& a, const CodeOutcome& r) {
        (*env)[9] = static_cast<std::uint8_t>(r.result);
        (*env)[15] = static_cast<std::uint8_t>(r.elapsed);
        a.game.world.hdr.options = static_cast<std::int16_t>(r.options);
        return resumeGame(a, env);
    });
}

// COVERT.EXE's programs[ENV[0]].
std::unique_ptr<Scene> afterGame(App& app, std::shared_ptr<CityEnv> env, int cityExit) {
    switch (cityExit) {
    case 1: case 7: case 8: case 10: case 13: return startTac(app, env, cityExit);
    case 2: case 15: return startCode(app, env);
    case 3: case 5: case 6: case 14: return startChase(app, env, cityExit);
    case 4: case 12: case 16: return startBug(app, env, cityExit);
    case 9: return makeEndOfCaseScene(app);
    default: return makeScene(app, "main-menu");
    }
}

// The test scenes' case: a new character (Max, the given difficulty, average
// skills), plot, episode and generate_case with a fixed seed.
void generateTestCase(App& app) {
    auto& g = app.game;
    auto envInt = [](const char* n, int d) {
        const char* v = std::getenv(n);
        return v && *v ? std::atoi(v) : d;
    };
    g.rng.seed(static_cast<std::uint32_t>(envInt("COVERT_CITY_SEED", 1234)));
    startNewCharacter(g);
    chooseGender(g, 0);
    g.career.codename.set("Test");
    chooseDifficulty(g, clampTo(envInt("COVERT_CITY_LEVEL", 1), 0, 3));
    std::array<int, 4> lvl{1, 1, 1, 1};
    finishTraining(g, lvl, 4);
    for (int k = 0; k < 8; ++k) {
        choosePlot(g, app.data);
        g.briefed = true;
        if (chooseEpisode(g).ok) break;
    }
    startCaseClock(g);
    g.storeCareer();
    CaseResult res = CaseGenerator(g.world, g.rng, app.data, caseSetupFor(g)).generate();
    (void)res;
    clearedForDuty(g.world);
    g.world.hdr.options = static_cast<std::int16_t>(g.world.hdr.options | 0xC);
}

std::uint32_t testSeed() {
    const char* v = std::getenv("COVERT_GAME_SEED");
    return v && *v ? static_cast<std::uint32_t>(std::strtoul(v, nullptr, 0)) & 0x7FFF : 4321;
}

}  // namespace

std::unique_ptr<Scene> makeCityNewCaseScene(App& app, const Frame&) {
    auto env = std::make_shared<CityEnv>();   // ENV[1] = 0: a new game
    return std::make_unique<CityLoopScene>(app, env, gameSeed());
}

std::unique_ptr<Scene> makeCityResumeScene(App& app, const Frame&) {
    auto env = std::make_shared<CityEnv>();
    (*env)[1] = 1;   // last_exit (ENV[7]) 0, ENV[15] 0
    return std::make_unique<CityLoopScene>(app, env, gameSeed());
}

REDUX_SCENE("city", [](App& app) -> std::unique_ptr<Scene> {
    generateTestCase(app);
    return std::make_unique<CityLoopScene>(app, std::make_shared<CityEnv>(), testSeed());
});

REDUX_SCENE("city-street", [](App& app) -> std::unique_ptr<Scene> {
    generateTestCase(app);
    {
        // The new game's start (mission briefing) without a screen.
        auto& g = app.game;
        g.rng.seed(testSeed());
        Sim sim(g.world, g.rng, app.data, g.career);
        sim.startNewGame();
        g.world.hdr.var50 = g.world.hdr.difficulty;
    }
    auto env = std::make_shared<CityEnv>();
    (*env)[1] = 1;
    return std::make_unique<CityLoopScene>(app, env, testSeed());
});

}  // namespace redux
