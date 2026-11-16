// The end of a case (covert-decomp FINAL/ENDCASE.C end_of_case, case_timeline and
// its timeline_* helpers, format_date_time, reveal_all_case_data,
// efficiency_report, career_summary, rogues_gallery; the GAMELIB suspect_dossier
// and pick_clue/describe_clue that the debriefing's MasterMind clue runs), as pure
// logic. The screens are src/app/EndCaseScene.cpp.
//
// What the original draws becomes display lists: each Page is the drawing the
// original does on the screen (g_gfx) up to a wait, in the original's order and
// coordinates, with the text texts assembled as it assembles them in text_buf.
// The scene applies the operations to its screen image and waits as the page says.
// random() is drawn exactly where the original draws it (the efficiency report's
// card positions, the vacation's extra picture, the rogues' gallery, pick_clue),
// and the arithmetic keeps the original's 16-bit ints, so the same Rng state gives
// the original's debriefing. Each phase is computed when the scene reaches it, so
// the draws stay in the original's order with the Hall of Fame's in between.
//
// FINAL is a fresh program at the end of every case (COVERT.EXE runs it when GAME
// exits with ENV[0] = 9): its globals start over, it reseeds the random numbers
// from the clock, and its BSS counters (num_masterminds, briefed) are 0.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/GameData.h"
#include "game/Career.h"

namespace redux {

class Sim;

// One drawing operation of the original, in its coordinates (see Kind).
struct DrawOp {
    enum class Kind {
        Fill,           // gfx_fill_rect(a, b, c, d, color e): c x d pixels
        Recolor,        // drv_recolor_rect(a, b, c, d, from e, to f)
        Line,           // draw_line(a, b, c, d, color e)
        Text,           // draw_text_transparent(text, a, b, color c)
        TextOpaque,     // draw_text(text, a, b, color c) on the port's fillcolor d
        TextCentered,   // draw_text_centered(text, a, b, color c)
        Wrapped,        // draw_wrapped_text(width a, b, c, color d) of text
        FaceParts,      // draw_face_parts(face a, b, c)
        Sprite,         // draw_sprite_a(n a, b, c): icons[n], the face parts' table
        FillFromPixel,  // gfx_fill_rect(c, d, e, f) with the colour of the pixel at (a, b)
        Icon,           // drv_copy_rect(icon_page, a << 4, 0, 16, 16, g_gfx, b, c): ICONS.PIC
        PageFrame,      // draw_page_frame(text, color a)
        CardPanel,      // draw_card_panel(a, b)
        Portrait,       // draw_portrait(face a, b, c)
        FaceFor,        // draw_face_for(leader a, b, c, 0, 0)
        Picture,        // load_picture_swapped(text, 1) shown whole (the back page copied)
    };
    Kind kind = Kind::Fill;
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    std::string text;
};

// The drawing up to a wait.
struct Page {
    enum class Wait {
        None,           // nothing: the next step follows at once
        Key,            // wait_key_or_button()
        KeyOrEsc,       // wait_key_or_button() == 27 ends the timeline
        Retrace,        // wait_retrace(retraces)
    };
    std::vector<DrawOp> ops;
    Wait wait = Wait::Key;
    int retraces = 0;
    // The timeline's state when this page waits (case_items, items_taken, the
    // items' holders): what counts if the player presses Esc here.
    std::uint16_t caseItems = 0, itemsTaken = 0;
    std::array<std::int8_t, 4> holders{};
};

// describe_clue's steps for the debriefing's MasterMind clue: the dossier as
// suspect_dossier draws it, then the news (show_menu(text, 20, 60)); the last
// step has no news and waits for a key.
struct ClueStep {
    std::vector<Page> dossier;          // the last page has Wait::None
    std::optional<std::string> notice;
};

class EndOfCase {
public:
    EndOfCase(GameState& g, const GameData& data);
    ~EndOfCase();
    EndOfCase(const EndOfCase&) = delete;
    EndOfCase& operator=(const EndOfCase&) = delete;

    // FINAL main with start_mode 0: srand(time(0) & 7FFFh) (COVERT_SEED in the
    // environment overrides it), the fresh globals (briefed = 0), load_env, then
    // options |= 8.
    static void startFinal(GameState& g);

    // end_of_case's first text (wrap_text_buffer(40), show_menu(8, 149)).
    static std::string introText();
    // The rest of end_of_case's start, up to case_timeline: num_participants, the
    // known flags kept in persons[].points, reveal_all_case_data, the counters.
    void begin();
    // case_timeline. The pages' end state is applied; on Esc at page i, call
    // timelineEscaped(pages[i]).
    std::vector<Page> timeline();
    void timelineEscaped(const Page& p);
    // efficiency_report: the cards (random(14) after each); sets ep().
    std::vector<Page> efficiencyReport();
    int ep() const { return ep_; }
    unsigned caseResult() const { return caseResult_; }
    int numParticipants() const { return numParticipants_; }

    // The plot's story of this episode: plot.txt key "*PLnne0" ("*PLnne5" if the
    // crime was committed), shown over briefing.pan with set_scene_var(0, storyVar()).
    std::string storyKey() const;
    int storyVar() const { return (caseResult_ & 0x40) ? 4 : 3; }

    // After the story: the episode done (crime committed), or the interrogation's
    // clue about Mr. Big. Returns the "Interrogation of the suspects ..." text when
    // the clue is given (then call mastermindClue()).
    std::optional<std::string> afterStory();
    std::vector<ClueStep> mastermindClue();
    // "You have captured the MasterMind ..." (wrap_text_buffer(38)), if he was.
    std::optional<std::string> capturedText() const;

    // vacation_snapshot(clamp(ep * (difficulty + 1) / 150, cur_month != 0, 20), 1).
    int vacationIndex() const;
    // Its random(3) for the extra picture (kind <= 1 only), drawn when the
    // pictures are loaded.
    bool vacationExtra(int kind);

    // After the Hall of Fame (hall_of_fame_insert(ep) is FameScene's): cur_month++,
    // the case's line in the history, the MasterMind's bit.
    void recordCase();
    // career_summary (counts num_masterminds).
    std::vector<Page> careerSummary();
    // The career points and the rank: the promotion text when the rank improved
    // (shown with set_scene_var(0, 1), wrap_text_buffer(36)).
    std::optional<std::string> promotion();
    // The end of the career (all 26 MasterMinds, or ten years): text_buf for the
    // rogues' gallery's window.
    std::optional<std::string> retirement() const;
    // rogues_gallery's drawing (random positions, a pause after each face); its
    // window comes after: show_menu(text, rogueMenuX(), 0x40).
    std::vector<Page> roguesGallery();
    int rogueMenuX() const;

    // The continue menu ("Do you want to...").
    std::string continueMenu() const;
    static std::string difficultyMenu();
    // "Change Difficulty Level": difficulty = clamp(result, 0, 3).
    void changeDifficulty(int menuResult);
    // "Save Game": options |= 40h, clock_base = 0, here_city = 1 before
    // save_game_menu (a save between cases, which loads into the next case).
    void beforeSave();
    static std::string afterSaveMenu() { return "Do you want to...\n Continue Game\n End Game\n"; }

    // format_date_time (with cur_month): "HH:MM AM Mon DD".
    std::string formatDateTime(int t) const;

    GameState& game() { return g_; }

private:
    World& w() { return g_.world; }
    Career& c() { return g_.career; }
    int rnd(int n) { return g_.rng.below(n); }
    void restoreOrgFlags(const std::array<std::uint16_t, 16>& f);
    std::array<std::uint16_t, 16> orgFlags() const;
    void loadMessageText(int p);            // FINAL's (no org flags)
    void truncate(int w);                   // truncate_text_to_width
    std::string& tb();                      // text_buf (the Sim's)
    // case_timeline's helpers, appending to `ops`.
    void drawPerson(std::vector<DrawOp>& ops, int n);
    void drawLabel(std::vector<DrawOp>& ops, int x, int y, int n);
    void drawLink(std::vector<DrawOp>& ops, int person);
    void drawIcon(std::vector<DrawOp>& ops, int n);
    void scroll(std::vector<DrawOp>& ops, int person);
    void drawEvent(std::vector<DrawOp>& ops, int person);
    void drawMarker(std::vector<DrawOp>& ops, int person);
    void drawPersonB(std::vector<DrawOp>& ops, int person);
    // suspect_dossier(who, 0, 0) on the screen, with its state changes.
    std::vector<Page> dossier(int who);
    void describeClue(int p, unsigned newk, std::vector<ClueStep>& out);

    GameState& g_;
    std::unique_ptr<Sim> sim_;
    int numParticipants_ = 0;
    unsigned caseResult_ = 0;
    std::int16_t epEarned_ = 0, epPossible_ = 0;
    std::uint16_t itemsTaken_ = 0, caseItems_ = 0;
    int ep_ = 0;
    int clue_ = -1;
    int numMasterminds_ = 0;
};

// For testing the debriefing on its own: a new character (Local Disturbance unless
// `difficulty`), a case generated and played by the simulation for `days` days
// with a few arrests, from a fixed seed; ends where GAME exits to FINAL.
void makeDemoFinishedCase(GameState& g, const GameData& data, std::uint32_t seed, int difficulty = 1,
                          int days = 12);

}  // namespace redux
