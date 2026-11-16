// TAC's case hooks: the game library routines TAC.EXE calls during a case's
// break-in (covert-decomp TAC/ACTION.C, through its own copy of GAMELIB CLUE.C
// and PERSON.C), run on the case's tables.
//
// TAC loads env.sve, so the library works on GAME's tables (persons, sites,
// clues, msgs, suspects, cities, orgs, city_clue_count) and save_env hands them
// back: here the routines run on the World directly, through GAME's library port
// (game/Sim.h) with TAC's state where TAC's DGROUP differs from GAME's:
//   - random() is TAC's stream (Tac's Rng: the randomize() after the building);
//   - text_buf is TAC's msg_buf, face_shown its popup_face, difficulty TAC's
//     own (adjusted for skill and alert; take_photo puts the case's back for
//     describe_new_info), seen_orgs its known_plans, city_clue_count[0] its
//     guards_near, here_org / here_city its cur_org / cur_city;
//   - TAC's persons[] (Tac::persons) is copied to the World before each call
//     and back after;
//   - MSGTEXT.C without GAME_MSGTEXT, persons[-1] and the neighbours of msgs[]
//     as in TAC's DGROUP (Sim::setTacLibrary);
//   - the library's other globals (popup_flag, clue_icon, subst_vals ...) start
//     as TAC's BSS / data do, in a Sim of its own.
// What the routines draw goes to Tac::libSteps, drawn with the same calls as
// GAME's screens (game/Paper.h, Face.h, formats/TextWindow.h) on TAC's screen;
// TAC's draw_intercept_popup and show_menu stand in for the library's calls to
// them, as the linker resolves them in TAC.EXE. No SDL.
#pragma once

#include <memory>

#include "core/GameData.h"
#include "game/Career.h"
#include "game/Sim.h"
#include "game/Tac.h"
#include "game/World.h"

namespace redux {

class TacCaseHooks : public TacHooks {
public:
    TacCaseHooks(World& world, const GameData& data, Career& career);
    ~TacCaseHooks() override;

    void suspectDossier(Tac&, int who) override;
    void appendPersonName(Tac&, int p) override;
    void personName(Tac&, int p) override;
    int describeNewInfo(Tac&, int org, int city, int amount, int popup) override;
    void showMasterPlan(Tac&, int org, int reveal) override;
    int orgAlertLevel(Tac&, int city, int org) override;
    int appendBuildingType(Tac&, int city, int org) override;
    int setLocationKnown(Tac&, int org, int city, int flags) override;
    int loadClueText(Tac&, int suspect, int full) override;
    void appendNumber(Tac&, int num) override;
    int revealRandomClue(Tac&, int n, int org, int city) override;

    // The library's own trace of what it showed (dossiers, clue cards, text
    // windows, new information), for tests: one line per event.
    std::vector<std::string> trace;

    // TacSetup from the World as TAC's main() reads env.sve (ENV[0] = program,
    // ENV[12] the rescue's person; runSeed is randomize()'s seed).
    static TacSetup setupFrom(const World& w, int program, int env12, std::uint32_t runSeed);
    // What TAC's main() leaves in the tables at the end (persons[], seen_orgs,
    // city_clue_count[0], the site's rooms_seen) before save_env.
    static void applyResult(World& w, const TacResult& r);
    // set_location_known(org, city, 0): the first site of (org, city), -1 if none
    // or its flags are 0.
    static int knownSite(const World& w, int org, int city);

private:
    class Ui;
    class Neighbours;
    struct Bind;

    void push(Tac& t);
    void pull(Tac& t);

    World& w_;
    const GameData& data_;
    Career& career_;
    std::unique_ptr<Sim> sim_;
    std::unique_ptr<Ui> ui_;
    std::unique_ptr<Neighbours> nb_;
    Tac* tac_ = nullptr;
    int gameDifficulty_ = 0;
};

}  // namespace redux
