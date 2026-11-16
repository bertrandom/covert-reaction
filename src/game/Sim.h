// The world simulation of the city loop (GAME.EXE) as pure logic: the clock and
// the daily events (covert-decomp GAME/TIME.C advance_time, process_events,
// check_out_of_hiding, check_special_location, remove_location_type,
// format_date_time), the chronology / message table (CHRONO.C
// add_chronology_entry), the game library's clue machinery (GAMELIB/CLUE.C
// describe_new_info, pick_clue, describe_clue, reveal_random_clue, show_clue,
// encode_clue, load_clue_text, clue_text, describe_person, append_number,
// suspect_dossier, format_message_event, delete_clue), the message texts
// (GAMELIB/MSGTEXT.C as GAME links it, MSGTXTG.C: load_message_text,
// compose_intercept_text, make_msg_key), the names and text helpers of
// GAMELIB/PERSON.C, the alertness rules of TRAVEL.C (raise_alertness,
// describe_alert_level) and the state changes behind the city screens (CITY.C,
// SCREENS.C, SUSPECTS.C, BRIEFING.C mission_briefing).
//
// What the original draws becomes data: text the original assembles in text_buf
// and shows (teletype messages, text windows, clue cards, dossiers) is appended
// to an event list, in order, with the text_buf contents at the moment the
// original shows them. Waits for a key are events too (with text_buf), so the
// screens can pace them. random() is called exactly where the original calls it,
// including inside its drawing loops (the clue cards' random widths), and all
// arithmetic keeps the original's 16-bit int semantics, so the same Rng state
// gives the original's world, day after day (checked against the decomp's C
// compiled with MSC 5.1: tests/msc-game).
//
// Where the original reads outside its tables (persons[-1], msgs[-1] and
// msgs[128] next to their neighbours in GAME's DGROUP, suspects[] at the slot
// byte of a plot), the port reads what GAME.EXE has there; see Sim.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/GameData.h"
#include "game/Career.h"
#include "game/Locations.h"
#include "game/Rng.h"
#include "game/World.h"

namespace redux {

class TextDb;
class FontSet;

// Something the original shows, in order.
struct SimEvent {
    enum class Kind {
        Teletype,   // incoming_message(title): the teleprinter, text = text_buf
        Notice,     // show_menu(text) used as a message window (text_window)
        Wait,       // wait_key_or_button(): the screen waits for a key; text = text_buf then
        Clue,       // show_clue: a new clue card; index = clue slot, text = the card's text
        Dossier,    // suspect_dossier(person): index = person
        NewInfo,    // describe_new_info's message box (CODED MESSAGE / NEW INFORMATION ...)
        Popup,      // describe_new_info's tip (popup): text, index = face_shown (-1, -2 building)
        Decoded,    // show_decoded_message's page: text = the message, index = msgs[] entry
        CaseOver,   // process_events ended the case (GAME exits to FINAL, ENV[0] = 9)
    };
    Kind kind;
    std::string title;
    std::string text;
    int index = -1;
    // Notice: the window's position (show_menu(text, x, y)) and how text_window
    // runs it: kNoWait (dossier_state / paused: drawn, no wait), kNoSelect
    // (menu_flag: lines starting with a space aren't highlighted).
    int x = 16, y = 21;
    unsigned flags = 0;
    static constexpr unsigned kNoWait = 1, kNoSelect = 2;
};

// What the screens draw while the simulation runs: the original draws as it
// goes (the dossier, clue cards, the decoded message) and waits for keys in the
// middle of the rules. A UI given to Sim::setUi gets each event when it happens
// (and may block there for the player: a Wait, a Notice, a Teletype) and the
// drawing calls in the original's order. The defaults draw nothing.
class SimUi {
public:
    virtual ~SimUi() = default;
    // Returns the item chosen when the event is a menu (a Notice with items), else -1.
    virtual int event(const SimEvent&) { return -1; }
    // draw_status_bar, with format_date_time(clock_base); process_events draws it
    // each time the clock moves.
    virtual void statusBar(const std::string& /*dateTime*/) {}
    virtual void retrace(int /*n*/) {}                                      // wait_retrace(n)
    virtual void pageFrame(const std::string& /*title*/, int /*color*/) {}  // draw_page_frame
    virtual void teletypePaper() {}                                         // draw_teletype_paper
    virtual void text(const std::string&, int /*x*/, int /*y*/, int /*color*/) {}        // draw_text_transparent
    virtual void textOpaque(const std::string&, int /*x*/, int /*y*/, int /*color*/) {}  // draw_text
    virtual void line(int /*x1*/, int /*y1*/, int /*x2*/, int /*y2*/, int /*color*/) {}
    // draw_wrapped_text(width, x, y, color) of `text` (the layout is Sim's).
    virtual void wrapped(const std::string& /*text*/, int /*width*/, int /*x*/, int /*y*/, int /*color*/) {}
    virtual void portrait(int /*face*/, int /*x*/, int /*y*/) {}           // draw_portrait
    virtual void sprite(int /*n*/, int /*x*/, int /*y*/) {}                // draw_sprite_a
    virtual void cardPanel(int /*x*/, int /*y*/) {}                        // draw_card_panel
    virtual void bevelBox(int /*x*/, int /*y*/, int /*w*/, int /*h*/, int /*color*/) {}   // draw_bevel_box
    virtual void clearPlayArea() {}                                        // clear_play_area
    virtual void clearScreen() {}                                          // clear_screen
    virtual void fillRect(int /*x*/, int /*y*/, int /*w*/, int /*h*/, int /*color*/) {}   // gfx_fill_rect
    // drv_copy_rect between the screen and the back page (same rectangle).
    virtual void toBackPage(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}
    virtual void fromBackPage(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}
    virtual void screenWidth(int /*w*/) {}                                 // screen_width = w
    virtual void portraitPos(int /*x*/, int /*y*/) {}                      // portrait_x, portrait_y
    // set_scene_var / set_scene_background(name, x, y): the animation the next
    // text window plays.
    virtual void sceneVar(int /*i*/, int /*v*/) {}
    virtual void sceneBackground(const std::string& /*name*/, int /*x*/, int /*y*/) {}
};

// GAME's globals that are not in env.sve (TIME.C, CITY.C, the library's text
// buffers). They start as the original's (BSS 0, the initialized ones as in DGROUP).
struct SimGlobals {
    int clockMinute = 0, clockHour = 0, clockDay = 0;   // format_date_time's outputs
    int hidingTarget = 0;                               // hiding_target
    int mastermind = 0;                                 // TIME.C mastermind
    int inBuilding = 0;                                 // in_building: time passes off the street
    int faceShown = 0;                                  // face_shown (portrait)
    int clueIcon = 0;                                   // clue_icon
    int popupFlag = 0;                                  // popup_flag
    std::string textBuf;                                // text_buf
    // MSGTEXT.C subst_vals: $VICTIM $SNDORG $RCVORG $SNDLOC $RCVLOC $HLPORG $OBJECT,
    // also CLUE.C's $KEY $NAME $ORG $CITY $ROLE (the first five). They keep their
    // values between lookups, as in the original.
    std::array<std::string, 7> subst{std::string(51, ' '), std::string(25, ' '), std::string(25, ' '),
                                     std::string(25, ' '), std::string(25, ' '), std::string(25, ' '),
                                     std::string(51, ' ')};
    std::string msgKey = "*MSG0000";                    // msg_key
};

// What lies next to msgs[] in the running program's DGROUP, where
// format_message_event reads and writes msgs[-1] and msgs[128] (i is -1 or 128).
// Without one, GAME's neighbours (see Sim.cpp); TAC's copy of the library has its
// own (game/TacCase.cpp).
class SimNeighbours {
public:
    virtual ~SimNeighbours() = default;
    virtual std::uint16_t msgFlags(int i) = 0;
    virtual void orMsgFlags(int i, unsigned v) = 0;
    virtual std::int16_t msgTime(int i) = 0;
    virtual int msgReceiverCity(int i) = 0;
};

class Sim {
public:
    Sim(World& world, Rng& rng, const GameData& data, Career& career);
    ~Sim();

    World& world() { return w_; }
    // The screens drawing the simulation (nullptr: none, events are only listed).
    void setUi(SimUi* ui) { ui_ = ui; }
    SimUi* ui() const { return ui_; }
    // The library as another program (TAC.EXE, BUG.EXE) links it: MSGTEXT.C
    // without GAME_MSGTEXT (load_message_text leaves orgs[].flags alone), and
    // that program's persons[-1] and neighbours of msgs[] (nullptr: GAME's).
    void setProgramLibrary(const Person& personM1, SimNeighbours* neighbours) {
        tacLibrary_ = true;
        personM1_ = personM1;
        neighbours_ = neighbours;
    }
    SimGlobals& globals() { return g_; }
    const SimGlobals& globals() const { return g_; }

    // Events since the last call.
    std::vector<SimEvent> takeEvents() { return std::exchange(events_, {}); }
    const std::vector<SimEvent>& events() const { return events_; }
    // True once process_events has ended the case (the original saves env.sve and
    // exits to FINAL's debriefing, from wherever it was called). After that
    // nothing advances, and the routines that called it return early.
    bool caseOver() const { return caseOver_; }

    // ---- TIME.C ----
    void advanceTime(int minutes);                 // advance_time
    void processEvents(int minutes, bool force);   // process_events
    int checkOutOfHiding(int n);                   // check_out_of_hiding
    int checkSpecialLocation(int p);               // check_special_location
    void removeLocationType(int p, unsigned bits); // remove_location_type
    std::string formatDateTime(unsigned t);        // format_date_time (sets clock_minute/hour/day)
    void drawStatusBar() {                         // draw_status_bar (and the clock's globals)
        const std::string t = formatDateTime(clockBase());
        if (ui_) ui_->statusBar(t);
    }

    // ---- CHRONO.C ----
    void addChronologyEntry(int kind, int msg);    // add_chronology_entry

    // ---- MSGTEXT.C (GAME's copy, MSGTXTG.C) ----
    // These append to text_buf as the original does.
    int lookupText(const char* file, const std::string& key, int nsubst, bool clueKeywords = false);
    void lookupTextSimple(const char* file, const std::string& key) { lookupText(file, key, 7); }
    void lookupRandomText(const char* file, const char* key, int n);   // lookup_random_text
    void loadMessageText(int p);                   // load_message_text
    void composeInterceptText(int n);              // compose_intercept_text (into text_buf)
    void makeMsgKey(int day, int msg);             // make_msg_key (into msg_key)

    // ---- PERSON.C ----
    void appendPersonName(int p);                  // append_person_name
    void personName(int p);                        // person_name
    int appendBuildingType(int city, int org);     // append_building_type
    int orgAlertLevel(int city, int org) const;    // org_alert_level
    bool orgCanReach(int org, int org2, int city) const;
    static int cityDistance(unsigned a, unsigned b) { return Locations::cityDistance(a, b); }
    int cityMapDistance(int c1, int c2) const;     // city_map_distance
    void wrapTextBuffer(int width);                // wrap_text_buffer
    void garbleMessage(unsigned mask, int n);      // garble_message
    // draw_wrapped_text's layout: the y after drawing text_buf at y with `width`
    // columns of 8 pixels (the original's font 1 widths); draws nothing.
    int drawWrappedText(int width, int y) const;
    int textWidth(const std::string& s) const;     // text_width

    // ---- CLUE.C ----
    int describeNewInfo(int org, int city, int amount, int popup);   // describe_new_info
    int pickClue(int org, int city, unsigned known, int show);       // pick_clue
    void describeClue(int p, unsigned newk);                         // describe_clue
    int addLocation(int org, int city, unsigned flags);              // add_location
    int setLocationKnown(int org, int city, unsigned flags);         // set_location_known
    void clearLocation(int org, int city);                           // clear_location
    void freeLocationIfUnused(int i);                                // free_location_if_unused
    int revealRandomClue(int n, int a, int b);                       // reveal_random_clue
    void deleteClue(int n);                                          // delete_clue
    void showClue(int org, int city, int suspect, int kind);         // show_clue
    int encodeClue(int ci, int kind);                                // encode_clue
    int loadClueText(int suspect, int full);                         // load_clue_text
    void describePerson(int ci);                                     // describe_person
    static int clueKindIndex(int kind, int text);                    // clue_kind_index
    void describePersonId(int ci, int id);                           // describe_person_id
    void appendNumber(int num);                                      // append_number
    void clueText(int kind, int text, int ci);                       // clue_text
    void clueTextWrapper(int ci) { clueText(clue(ci).kind, clue(ci).text, ci); }
    void suspectDossier(int who, int x, int y);                      // suspect_dossier
    void formatMessageEvent(int e);                                  // format_message_event

    // ---- TRAVEL.C ----
    void raiseAlertness(int org, int city, int amount);              // raise_alertness
    int describeAlertLevel(int org, int who);                        // describe_alert_level (appends)
    // travel_screen's state change once a city is picked: the flight's frame count
    // comes from fly_to_city (which draws random(8) first).
    int flyToCityFrames(int from, int to);                           // fly_to_city's random() and frames
    void travelTo(int city);                                         // travel_screen after the choice

    // ---- BRIEFING.C / GAME.C ----
    void missionBriefing();                        // mission_briefing's loop (new case)
    void startNewGame();                           // GAME main, new game: briefing, here_org, options
    void resumeGame(int minutesAway);              // GAME main, coming back from a mini-game
    int setExitState();                            // set_exit_state's ENV+48h (describe_alert_level)

    // ---- SCREENS.C / SUSPECTS.C (state changes behind the screens) ----
    void hotelRest(int steps);                     // hotel_rest: `steps` x process_events(60) (until a key)
    void hotelLounge();                            // hotel_screen "Visit the lounge"
    int localScan();                               // cia_hq_screen Intelligence: Local Scan
    int internationalScan();                       // ... International Scan
    bool accuseDoubleAgent();                      // ... Accuse Double Agent (true: was a mole)
    void researchOffice();                         // research_office (Check with Sam)
    void suspectsOverviewMarks();                  // suspects_overview's 4000h marks
    void showPersonClues(int sus);                 // show_person_clues
    void showDecodedMessage(int n);                // show_decoded_message (after the crypto puzzle)

    // ---- CITY.C ----
    // cia_contact_screen when last_exit == 0 (fromStreet): raise_alertness, the
    // orgs_visited bit, and at difficulty 0 the research office.
    void ciaContactScreen(int org, int city, bool fromStreet);
    // city_loop after the city menu (choice, menu_count): the ambush check. The
    // organization whose agents attack (ENV[12], city_exit 10), or -1.
    int cityAmbushCheck(int choice, int menuCount);
    // watch_building up to its menu: the time spent and the person seen leaving.
    struct WatchResult {
        int who = 0;          // person slot (face_shown = his id)
        int bestOrg = 0;      // here_org after "Follow car" / "Trace car"
        int choice = -1;      // the menu's answer when a UI asked it (0 Wait .. 3 Check Data)
    };
    WatchResult watchBuilding(int org, int city);
    void watchBuildingWait(int who);               // "Wait": forget a passer-by
    // building_screen "Place wiretap" after BUG: ENV[8] score, ENV[9] alarms.
    // `scorex` persists like the original's local (unset when !firstVisit).
    int wiretapResult(int org, int city, int score, int alarms, bool firstVisit, int& scorex);
    // What a mini-game (TAC, CHASE) left in the env block.
    struct MiniGameResult {
        int lastExit = 1;     // last_exit (0: success_briefing without a break-in)
        int env8 = 0;         // (char)ENV[8]: score, -1 captured
        int env9 = 0;         // (char)ENV[9]: alert amount
        int env10 = 0;        // ENV[10]: 1 found an item holder, 2 captured a suspect
        int env13 = 1;        // ENV[13]: 0 = the prisoner was turned (double agent)
        int scorexIn = 0;     // the score when lastExit == 0
        bool agreeExchange = false;   // the answer to the prisoner exchange if captured
    };
    // success_briefing: returns city_exit (7: escape after capture) or 0.
    int successBriefing(int org, int city, const MiniGameResult& res);
    void interrogateSuspect(int who, bool doubleAgent);   // interrogate_suspect (dbl: ENV[13] == 0)
    void successBriefingText(int n);               // success_briefing_text: item n recovered
    int capturedScreen(bool agree);                // captured_screen: 7 (escape) or 0 (exchanged)
    bool prisonerExchange(bool agree);             // prisoner_exchange (the menu's answer)

    // draw_wrapped_text(width, x, y, color) of text_buf: drawn by the UI, laid out
    // here; returns the y below.
    int drawWrapped(int width, int x, int y, int color);

    // CITY.C show_prose: a prose.dta passage appended to text_buf, wrapped at `width`.
    void showProse(const char* key, int width, const std::string& rplc = {}, int n = 0);

    // ---- accessors with the original's out-of-table reads ----
    Person& person(int i);
    Suspect& suspect(int i);
    Plot& plot(int i);
    Clue& clue(int i) { return w_.clues[static_cast<std::size_t>(i)]; }

    // 16-bit views of the env header.
    unsigned clockBase() const { return static_cast<std::uint16_t>(w_.hdr.clockBase); }
    void setClockBase(unsigned v) { w_.hdr.clockBase = static_cast<std::int16_t>(v); }
    int difficulty() const { return w_.hdr.difficulty; }
    int activeSuspects() const { return w_.hdr.numSuspects - difficulty() * 4; }
    std::int16_t& heat() { return w_.hdr.var52; }
    std::uint16_t moleCities() const { return static_cast<std::uint16_t>(w_.hdr.doubleAgents); }
    std::int16_t& cityClueCount(int c) { return w_.cityClueCount[static_cast<std::size_t>(c)]; }
    std::int16_t& orgClueCount(int o) { return w_.cityClueCount[static_cast<std::size_t>(16 + o)]; }

private:
    int emit(SimEvent::Kind k, std::string title = {}, int index = -1);
    int emitText(SimEvent::Kind k, std::string title, std::string text, int index = -1);
    int post(SimEvent e);
    void teletype(const char* title) { emit(SimEvent::Kind::Teletype, title); }
    int notice(const std::string& s, int x = 16, int y = 21, unsigned flags = 0);   // show_menu(s, x, y)
    void waitKey() { emit(SimEvent::Kind::Wait); }
    void endCase();
    int rnd(int n) { return static_cast<std::int16_t>(rng_.below(n)); }
    Locations loc() { return Locations(w_, rng_, career_.mm); }
    std::uint16_t msgFlags(int i) const;            // msgs[i].flags, i may be -1 or 128
    void orMsgFlags(int i, unsigned v);
    std::int16_t msgTime(int i) const;
    int msgReceiverCity(int i) const;
    const TextDb& textDb(const char* file);
    void proseSubstitute(const std::string& key, const std::string& val);

    World& w_;
    Rng& rng_;
    const GameData& data_;
    Career& career_;
    SimGlobals g_;
    SimUi* ui_ = nullptr;
    std::vector<SimEvent> events_;
    bool caseOver_ = false;
    Person personM1_;                               // persons[-1]
    bool tacLibrary_ = false;                       // see setProgramLibrary
    SimNeighbours* neighbours_ = nullptr;
    Suspect suspectOut_;                            // suspects[] past the table
    Plot plotOut_;                                  // plots[] outside the table
    std::map<std::string, std::string> proseKeys_;  // show_prose's edited key literals
    std::unique_ptr<TextDb> text_, clues_, prose_;
    std::unique_ptr<FontSet> fonts_;
};

}  // namespace redux
