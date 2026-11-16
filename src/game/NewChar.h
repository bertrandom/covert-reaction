// A new character and the plot of his career (covert-decomp FINAL/NEWCHAR.C
// create_new_character, enter_codename, training_screen, plot_briefing,
// case_intro_story), without the screens: the scenes in src/app/NewCharScene.cpp
// draw them and call these in the original's order, so the random() draws match.
//
// Where the results live (as in FINAL):
//   gender          world.hdr.options bit 2 (Maxine)
//   difficulty      world.hdr.difficulty (0..3)
//   skill levels    world.hdr.skillLevels (2 bits per skill, | 500h)
//   code name, plot, episode, crime, world, MasterMind: career (the player block)
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/GameData.h"
#include "formats/TextDb.h"
#include "game/CaseGen.h"
#include "game/Career.h"

namespace reaction {

// plot_defs: plot.txt's plots (*PLnn).
struct PlotDef {
    const char* name;
    unsigned orgs;                 // ANDed with Org.flags: organizations that may run it
    std::array<int, 8> crime;      // crimeN.dta of each episode, -1 none
    std::array<std::array<const char*, 2>, 8> roles;  // the briefing's two roles per episode
};

extern const std::array<PlotDef, 16> kPlotDefs;
extern const std::array<const char*, 13> kCrimeNames;       // ENDCASE.C crime_names
extern const std::array<const char*, 26> kMastermindNames;  // ENDCASE.C, by leader code
extern const std::array<const char*, 12> kMonthNames;       // GAMELIB CLUE.C month_names
extern const std::array<const char*, 5> kSkillNames;        // "Combat", ... ("Stamina" unused)
extern const std::array<std::uint8_t, 4> kRatingColors;     // training_rating's boxes

// The menus' texts.
inline constexpr const char* kGenderMenu = "Select one...\n Maximillian Remington\n Maxine Remington\n";
inline constexpr const char* kDifficultyMenu =
    "Which difficulty level?\n Local Disturbance\n National Threat\n Regional Conflict\n Global Crisis\n";
std::string trainingMenu(bool practice);
// training_rating: "Average" .. "Awesome".
const char* ratingName(int level);

// create_new_character's resets (before the gender menu).
void startNewCharacter(GameState& g);
// The gender menu: item 1 sets options bit 2. Max (0, or Esc) leaves it as it was:
// the original never clears it (so a Maxine snapshot seen in the Hall of Fame
// carries over).
void chooseGender(GameState& g, int menuResult);
// The difficulty menu: clamp(result, 0, 3) (Esc = Local Disturbance).
void chooseDifficulty(GameState& g, int menuResult);

// enter_codename's editor (len characters, DOS key codes: 13 Enter, 8 Backspace,
// 4B00h/4D00h Left/Right, 5200h Insert, 5300h Delete, 20h-7Fh characters).
class CodenameField {
public:
    explicit CodenameField(int len = 15) : len_(len), buf_(static_cast<std::size_t>(len), ' ') {}
    // Returns true on Enter.
    bool key(int k);
    const std::string& buffer() const { return buf_; }  // padded with spaces to len
    int cursor() const { return cursor_; }
    // The name as stored: trailing spaces cut, but never the first character.
    std::string result() const;

private:
    int len_;
    std::string buf_;
    int cursor_ = 0;
    bool first_ = true;
};

// training_screen's end: the points not spent go to the first skills, then (after
// the ratings are shown: `shown` receives them) the original's quirk drops
// lvl[4 - total] from Excellent to Good, and skill_levels gets the levels (0x55 on
// Local Disturbance) | 500h.
void finishTraining(GameState& g, std::array<int, 4>& lvl, int total, std::array<int, 4>* shown = nullptr);

// plot_briefing's choices: the plot, the world, the MasterMind (random draws,
// loads worldN.dta). Returns the first key of the briefing ("*PLnn90").
std::string choosePlot(GameState& g, const GameData& data);

// The pages of a story in plot.txt: `key`, then key with its last character
// incremented, while found; each page wrapped at 40 characters (wrap_text_buffer).
std::vector<std::string> storyPages(const TextDb& plot, std::string key);

// case_intro_story's episode draw. ok == false: no episode left (the original then
// runs plot_briefing again and retries).
struct EpisodeChoice {
    bool ok = false;
    bool showStory = false;        // !briefed || episode == 7
    std::string storyKey;          // "*PLnneA" / "*PLnnea"
};
EpisodeChoice chooseEpisode(GameState& g);
// case_intro_story's resets at the end (num_msgs, clock_base, game_time,
// num_sites, seen_orgs = 0).
void startCaseClock(GameState& g);

// What generate_case needs (CaseGen.h), from the career: crime, world,
// difficulty, the MasterMind, cur_month.
CaseSetup caseSetupFor(const GameState& g);

}  // namespace reaction
