// The car chase (CHASE.EXE): covert-decomp src/CHASE/CHASE.C (main, the env
// block), CITY.C (generate_city_map, check_building_reached, format_clock) and
// DRIVE.C (init_cars, car_select_screen's car specs, chase_loop, car_in_view,
// road_open). The game logic only: the screens are app/ChaseScene.cpp.
//
// The city is a 24x24 grid of blocks, 32 map pixels each; cars drive along the
// blocks' edges and turn at the corners (positions that are multiples of 32).
// city_map[x][y] (map(x, y) here): high nibble = the street-view tile (chase.pic),
// low nibble = which sides of the block have a road (bit 0 top, 1 right,
// 2 bottom, 3 left): the road east from corner (x, y) is bit 0 of block (x, y),
// the road south is bit 3.
//
// Random numbers keep the original's order: randomize() (time seed), the four
// car offers of car_select_screen, then generate_city_map reseeds with
// city * 123 (+ map_seed, never written by CHASE: 0), so a city always gets the
// same streets and buildings and, with the same inputs, the chase plays the same.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/Rng.h"

namespace redux {

// What CHASE reads from the env block (CHASE.C main, read_env_params). The
// defaults are a practice chase as FINAL's start_practice(1) sets it up.
struct ChaseSetup {
    // ENV[0], the program number: 3 (GAME: follow a suspect) and 14 (practice)
    // give mode 0, 5 gives 1, 6 gives 2 (you are chased). Mode 1 plays as 0
    // without its end messages.
    int mode = 0;
    int sex = 1;                      // ENV[10] bit 0: the suspect is a man ("He")
    int cityId = 0;                   // env +20h here_city (city_id)
    int targetBuilding = 4;           // env +22h here_org: the building the suspect heads for
    int startBuilding = 0;            // ENV[9]: the building the chase starts at
    std::uint16_t options = 0x20;     // env +24h: 4 sound, 20h practice
    std::uint16_t gameTime = 0;       // env +30h clock base, minutes (night: before 8:00 or from 20:00)
    int difficulty = 0;               // env +3Ah
    std::uint16_t envFlags = 0x555;   // env +4Eh skill levels: bits 2-3 driving, 10-11 game speed
    int alertLevel = 0;               // env +48h
    std::uint16_t orgMask = 0;        // cities[city_id].org_mask (+1Ch): buildings shown and enterable
    std::array<std::string, 16> orgAbbrev{};   // orgs[n].abbrev: the map labels
    std::uint16_t mapSeed = 0;        // DGROUP 5C00h map_seed: never written, 0
    bool cga = false;                 // ENV[44h] == 'C'
    std::uint32_t seed = 1;           // randomize(): time(0) & 7FFFh
};

// What CHASE writes back (end of main).
struct ChaseResult {
    int outcome = 0;                  // ENV[9]: 0 lost / escaped, 1 suspect parked / run off the road, 2 arrest
    int score = 0;                    // ENV[10]: clamp(score * 25, 0, 100), how much the suspect noticed
    int nextProgram = 0;              // ENV[0]: 11h back to FINAL (practice), 0 GAME; FFh quit
    int minutes = 0;                  // ENV[15]: chase_clock / 60
    std::uint16_t envFlags = 0;       // env +4Eh with the game speed
    int buildingReached = -1;         // ENV[14] (and here_org), set by check_building_reached
    std::uint16_t options = 0;        // Alt-S toggles bit 2
    int chaseOver = 0;                // 1, or 2: the suspect noticed you and got away
    bool quitGame = false;            // Alt-Q, Yes
    std::string message;              // the end text_window
};

struct ChaseCar {                     // struct Car, 14h bytes
    int x = 0, y = 0;                 // map position, 32 per block
    int dir = 0;                      // 0 N, 1 E, 2 S, 3 W
    int follow = 0;                   // tracking the suspect
    int tx = 0, ty = 0;               // last known suspect position / destination
    int req = 0;                      // requested turn, -1 none
    int heat = 0;                     // how much the suspect has noticed this car
    int speed = 0;                    // 0..4, 20 mph each
    int bits = 0;                     // spec: 0-1 speed, 2 tracking, 3 handling, 4-5 visibility
};

// A car offered on the selection screen (car_select_screen).
struct CarOffer {
    int bits = 0;
    int sprite = 0;                   // row of cars.pic (50 pixels each)
    int paint = 0;                    // visibility grade 0..3
};

// The four offers: random(64) until the spec's total is 3 - difficulty.
std::array<CarOffer, 4> rollCarOffers(Rng& rng, int difficulty);

class Chase {
public:
    static constexpr int kBlocks = 24;
    static constexpr std::array<int, 4> kDx = {0, 1, 0, -1};
    static constexpr std::array<int, 4> kDy = {-1, 0, 1, 0};
    // wall_x, wall_y: the map lines closing each side of a block.
    static constexpr int kWallX[4][2] = {{2, 7}, {8, 8}, {2, 7}, {0, 0}};
    static constexpr int kWallY[4][2] = {{0, 0}, {2, 7}, {8, 8}, {2, 7}};

    // A dot of the city map (draw_map_dot), in map pixels (the screen is / 4).
    struct Dot {
        int x, y, color;
    };
    // A wall generate_city_map drew: block (x, y), side 0..3.
    struct Wall {
        int x, y, side;
    };
    // A building generate_city_map placed and labelled.
    struct Label {
        int building, x, y;
    };
    // A turn of the player's car the windshield animates (turn_animation).
    struct Turn {
        int dir, newDir, scene;
    };
    enum class KeyAction { None, Return, QuitPrompt, Pause };

    explicit Chase(const ChaseSetup& setup);

    // --- main()'s derived settings ---
    int mode() const { return mode_; }
    int difficulty() const { return difficulty_; }
    int gameSpeed() const { return gameSpeed_; }
    bool night() const { return night_; }
    bool cga() const { return setup_.cga; }
    Rng& rng() { return rng_; }
    // wait_retrace after each chase_loop: 8 slow, 4 normal, 0 fast.
    int loopWait() const { return gameSpeed_ == 1 ? 4 : gameSpeed_ ? 0 : 8; }

    // --- setup, in main's order ---
    std::array<CarOffer, 4> rollOffers() { return rollCarOffers(rng_, difficulty_); }
    void setTeamCar(int car, int bits) { cars_[static_cast<std::size_t>(car)].bits = bits; }
    void generateMap();               // generate_city_map (the drawing is recorded)
    void initCars();                  // init_cars

    // --- the city ---
    std::uint8_t map(int x, int y) const { return map_[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)]; }
    bool roadOpen(int x, int y, int dir) const;    // road_open
    int buildingX(int n) const { return bldX_[static_cast<std::size_t>(n)]; }
    int buildingY(int n) const { return bldY_[static_cast<std::size_t>(n)]; }
    int chaseStart() const { return chaseStart_; }
    int chaseDest() const { return chaseDest_; }
    const std::vector<Wall>& walls() const { return walls_; }
    const std::vector<Label>& labels() const { return labels_; }
    std::string labelText(int building) const;
    // The map's background: 8 at night (or CGA), else 7.
    int mapColor() const { return night_ || setup_.cga ? 8 : 7; }
    // The label colour: 15 CGA, 7 night, 9 day.
    int labelColor() const { return setup_.cga ? 15 : night_ ? 7 : 9; }

    // --- chase_loop, split where the original waits ---
    // The start (chase_clock 32: the team cars set off); true: flush_input.
    bool loopStart();
    // Keys are read once the team cars are off (chase_clock > 32).
    bool readsKeys() const { return clock_ > 32; }
    // One key of the switch. Return: chase_loop returns at once (Esc, arrest);
    // QuitPrompt / Pause: the scene asks / waits, then calls afterKey.
    KeyAction key(int k);
    void afterKey();                  // the reversing test after the switch
    // The cars' moves and the AI; dots() and turns() say what to draw.
    void moveCars();
    void endLoop() { ++clock_; }      // main: chase_clock++

    std::vector<Dot>& dots() { return dots_; }
    // Turns of the player's car this loop (two when a chased car's turn leads
    // into a dead end).
    std::vector<Turn>& turns() { return turns_; }

    // car_in_view(n): the distance at which car n sees the suspect along a
    // straight street, 0 if it doesn't (sets carAhead).
    int carInView(int n);
    bool carAhead() const { return carAhead_; }

    // --- state ---
    const ChaseCar& car(int i) const { return cars_[static_cast<std::size_t>(i)]; }
    int curCar() const { return curCar_; }
    int clock() const { return clock_; }
    int scene() const { return scene_; }
    void setScene(int s) { scene_ = s; }
    int heatLimit() const { return heatLimit_; }
    int chaseOver() const { return over_; }
    int outcome() const { return outcome_; }
    int score() const { return score_; }
    bool inRange() const { return inRange_; }
    int escapeTimer() const { return escapeTimer_; }
    int lostCount() const { return lostCount_; }
    std::uint16_t options() const { return options_; }

    // main's end: the message and the env block values. `quit`: Alt-Q, Yes.
    ChaseResult result(bool quit = false) const;
    // format_clock(t): "h:mm:ss", the hour's last digit only.
    std::string clockText() const;

private:
    bool checkBuildingReached(int x, int y);   // check_building_reached
    void dot(int x, int y, int c) { dots_.push_back({x, y, c}); }
    void requestTurn(int dir, int newDir);

    ChaseSetup setup_;
    Rng rng_;
    int mode_ = 0, gameSpeed_ = 1, difficulty_ = 0;
    bool night_ = false;
    std::uint16_t options_ = 0;
    int targetBuilding_ = 0;
    int buildingReached_ = -1;

    std::array<std::array<std::uint8_t, kBlocks>, kBlocks> map_{};
    std::array<int, 16> bldX_{}, bldY_{};
    int chaseStart_ = 0, chaseDest_ = 0;
    std::vector<Wall> walls_;
    std::vector<Label> labels_;

    std::array<ChaseCar, 3> cars_{};
    int curCar_ = 1;
    int clock_ = 0;
    int over_ = 0, outcome_ = 0, score_ = 0;
    int heatLimit_ = 1;
    bool inRange_ = false;
    int showTargets_ = 0;             // never set
    int escapeTimer_ = 0;
    int lostCount_ = 0;
    int turnToggle_ = 0;
    bool carAhead_ = false;
    int scene_ = 0;
    int targetX_ = 0, targetY_ = 0;

    std::vector<Dot> dots_;
    std::vector<Turn> turns_;
};

}  // namespace redux
