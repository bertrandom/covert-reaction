// GAME.EXE's program (covert-decomp src/GAME): main, the city loop and the
// building screens (CITY.C), travel (TRAVEL.C), the hotel, the CIA building and
// the data files (SCREENS.C), the clue overview (SUSPECTS.C), the chronology
// (CHRONO.C) and the master plan (PERSON.C show_master_plan), written as the
// original's straight-line code over the Sim (the rules) and CityUi (the
// screen). It runs on a GameThread; CityLoop.cpp's scene drives it and starts
// the program GAME exits to (a mini-game, FINAL's debriefing).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "app/App.h"
#include "app/CityUi.h"
#include "game/Sim.h"

namespace redux {

// What survives between two runs of GAME: the bytes of the env block the
// programs pass each other (ENV[0..15]; the tables and the header are in
// app.game.world) and the intercepted message for CODE (ENV+200h).
struct CityEnv {
    std::array<std::uint8_t, 16> b{};     // ENV[0..15]
    std::string intercept;                // ENV+200h
    std::uint8_t& operator[](int i) { return b[static_cast<std::size_t>(i)]; }
    std::uint8_t operator[](int i) const { return b[static_cast<std::size_t>(i)]; }
    int signedAt(int i) const { return static_cast<std::int8_t>(b[static_cast<std::size_t>(i)]); }   // (char)ENV[i]
};

class CityGame {
public:
    CityGame(App& app, CityEnv& env, CityUi& ui, std::uint32_t seed);

    // GAME.C main: returns city_exit (why GAME ended; the next program is
    // ENV[0] = city_exit, -1 quit). Throws CaseOverExit when process_events ends
    // the case (ENV[0] = 9).
    int run();

    Sim& sim() { return sim_; }

private:
    // GAME.C
    void setExitState();
    // CITY.C
    void cityLoop(int city);
    void buildingScreen();
    void watchBuilding(int org, int city);
    void successBriefing(int org, int city);
    // TRAVEL.C
    void travelScreen();
    void drawCityLabel(int c, int color);
    void flyToCity(int from, int to);
    // SCREENS.C
    void hotelScreen();
    void hotelRest();
    void dataFilesMenu();
    void ciaHqScreen();
    void activityReport();
    void wiretapsScreen();
    void organizationSummary();
    void citySummary();
    void documentsScreen();
    void suspectFilesScreen();
    int listSuspects();
    void codedMessagesScreen();
    // SUSPECTS.C
    void suspectsOverview();
    // CHRONO.C
    void chronologyScreen(bool mode);
    // PERSON.C
    void showMasterPlan(int org, bool reveal);

    // helpers over the Sim's text buffer and tables
    std::string& tb() { return sim_.globals().textBuf; }
    World& w() { return app_.game.world; }
    WorldHeader& h() { return app_.game.world.hdr; }
    Person& person(int i) { return sim_.person(i); }
    const std::string cityName(int c) { return w().cities[static_cast<std::size_t>(c & 15)].name.str(); }
    const std::string orgName(int o) { return w().orgs[static_cast<std::size_t>(o & 15)].name.str(); }
    const std::string orgAbbrev(int o) { return w().orgs[static_cast<std::size_t>(o & 15)].abbrev.str(); }
    int difficulty() const { return app_.game.world.hdr.difficulty; }
    void advanceTime(int m) { sim_.advanceTime(m); }

    App& app_;
    CityEnv& env_;
    CityUi& ui_;
    Sim sim_;

    // GAME's globals (BSS / DGROUP: they start over with each run of GAME)
    int lastExit_ = 0;         // last_exit
    int cityExit_ = 0;         // city_exit
    int firstVisit_ = 1;       // first_visit
    int leftBuilding_ = 0;     // left_building
    int menuCount_ = 0;        // menu_count
    std::array<int, 64> menuItems_{};   // menu_items
    int btypek_ = 0;           // building_screen's btypek (unset when the building isn't known)
    int scorex_ = 0;           // building_screen's scorex
    int listSel_ = 0;          // SUSPECTS.C list_sel
    int saveSlot_ = 0;         // OFFICE.C save_slot
};

}  // namespace redux
