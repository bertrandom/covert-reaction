// BUG's intercepts during a case: show_intercepted_info (covert-decomp
// BUG/BOARD.C) calls describe_new_info(here_org, here_city, amount, 1) from
// BUG.EXE's copy of GAMELIB on the tables it loaded from env.sve, with BUG's
// random() stream (randomize() before build_board, so the board's own calls and
// the library's interleave), and save_env hands the tables back to GAME.
//
// As TAC's hooks (game/TacCase.h), this runs GAME's library port (game/Sim.h)
// on the World directly, with BUG's state where its DGROUP differs: its own
// Sim (BUG's BSS: popup_flag, clue_icon, face_shown ... start at 0), MSGTEXT.C
// without GAME_MSGTEXT, BUG's persons[-1] (the end of logic[11]) and neighbours
// of msgs[]. difficulty is the case's (show_intercepted_info puts
// orig_difficulty back for the call). What it draws is recorded as LibSteps
// for the scene to replay; BUG's draw_intercept_popup draws a "waveform" with
// random() calls, which are made when the popup is recorded, in the original's
// order. No SDL.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/GameData.h"
#include "core/Image.h"
#include "game/Career.h"
#include "game/Face.h"
#include "game/LibScreen.h"
#include "game/Sim.h"
#include "game/Wiretap.h"
#include "game/World.h"

namespace reaction {

class BugCaseHooks {
public:
    BugCaseHooks(World& world, const GameData& data, Career& career);
    ~BugCaseHooks();

    // show_intercepted_info(amount)'s describe_new_info, now; returns the id of
    // what it drew and waited for (script()).
    int intercept(Wiretap& wt, int amount);
    const std::vector<LibStep>& script(int id) const { return scripts_[static_cast<std::size_t>(id)]; }
    const FaceSprites& faces() const { return faces_; }

    std::vector<std::string> trace;   // what the library showed (tests)

private:
    class Neighbours;

    World& w_;
    const GameData& data_;
    Career& career_;
    std::unique_ptr<Sim> sim_;
    std::unique_ptr<Neighbours> nb_;
    LibRecorder ui_;
    std::deque<std::vector<LibStep>> scripts_;
    FaceSprites faces_;
    Image camera_, camera2_;
    Wiretap* wt_ = nullptr;
};

}  // namespace reaction
