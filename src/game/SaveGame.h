// Saved games (covert-decomp FINAL/LOADGAME.C and GAME/OFFICE.C load_game_menu,
// list_save_files, save_game_menu, write_save_file; MPSLIB/ENV.C save_env and
// load_env with a file name): four slots, CV0.SVE .. CV3.SVE, in the original's
// format, so the original's saves load and Covert Reaction's load in the original.
//
// A save file is the env block from ENV+10h (3F0h bytes: the header words at
// ENV+20h..53h, see WorldHeader), then the player block (0x28E bytes, Career),
// the suspects, their state, the plot steps, cities, organizations, persons,
// sites, clues, messages, items and the clue counts (World::save). save_env sets
// env_var_30_copy = clock_base before writing and, for a file, the player block's
// cur_person_name to the current city's name (the listing's "where").
//
// The original asks for a drive (select_drive) and keeps the files on a floppy;
// Covert Reaction keeps them in the per-user folder (the caller's directory) and, when
// a slot has no file there, reads the game directory's copy (an original save).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "game/Career.h"

namespace reaction::savegame {

inline constexpr int kSlots = 4;

// "cv0.sve" .. "cv3.sve".
std::string slotFileName(int slot);

// list_save_files(name, 1): the three menu lines of a slot (each an item, so the
// menu's choice / 3 is the slot): the agent, rank, where and when, the difficulty;
// " (EMPTY)" if there's no file.
std::string slotListing(const std::optional<std::vector<std::uint8_t>>& file);

// The load/save menus' texts: "Select Load File...\n" / "Select Save File...\n"
// and the four listings. Shown with show_menu(text, 4, 16).
std::string slotMenu(bool loading, const std::array<std::optional<std::vector<std::uint8_t>>, kSlots>& files);

// save_env(name): the file's bytes. Updates env_var_30_copy and cur_person_name in
// the career and the player block, as the original does in memory.
std::vector<std::uint8_t> saveBytes(GameState& g);

// list_save_files(name, 0): load_env(name), then options &= ~80h (the line-up is
// asked again). False if the file can't be read as a saved game.
bool loadBytes(GameState& g, std::span<const std::uint8_t> bytes);

// After a load: FINAL's main menu keeps the joystick bit of the options it had;
// a save made between cases (options 40h) goes on to the next case's briefing,
// any other into the city loop (GAME).
inline bool savedBetweenCases(const GameState& g) { return (g.world.hdr.options & 0x40) != 0; }

// What the hotel's "Save Game" does before save_game_menu (SCREENS.C
// hotel_screen): options &= ~40h, ENV+50h = difficulty (GAME reads it back).
void beforeCitySave(GameState& g);

// Files: `userDir`'s slot file, else `gameDir`'s (case-insensitive), else none.
std::optional<std::filesystem::path> findSlot(const std::filesystem::path& userDir,
                                              const std::filesystem::path& gameDir, int slot);
std::array<std::optional<std::vector<std::uint8_t>>, kSlots> readSlots(const std::filesystem::path& userDir,
                                                                        const std::filesystem::path& gameDir);
// Writes the slot file in `userDir` (created if needed). Returns 0, -1 if it
// can't be created (the original's "File Error"), or the errno
// the original reports (13: write access denied, 28: disk full; others as 5).
int writeSlot(const std::filesystem::path& userDir, int slot, std::span<const std::uint8_t> bytes);

}  // namespace reaction::savegame
