// The code-breaking puzzle (CODE.EXE, the "crypto workstation"): covert-decomp
// src/CODE/CODE.C and CLOCK.C. Pure logic, no drawing: the cipher, the player's
// substitutions, the computer's help, the solved test and the clock. The screen
// (app/CodeScene.cpp) drives it and draws what the original draws.
//
// The original reads its setup from the env block COVERT.EXE keeps in memory
// (ENV.C save_env) and writes the result back; CodeSetup / CodeOutcome are those
// fields. Random numbers are drawn in the original's order: the key shuffle and
// computer_assist here, the scramble effect's positions through scramblePos().
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "core/GameData.h"
#include "game/Rng.h"

namespace redux {

// What CODE.EXE's main() reads from the env block.
struct CodeSetup {
    // ENV+200h (512 bytes): the intercepted message, as GAME's
    // coded_messages_screen (compose_intercept_text) or, for practice, FINAL's
    // compose_intercept leave it in text_buf: mixed case, lines joined by spaces
    // (so it ends with one), may hold colour bytes (80h, 8Fh).
    std::string message;
    // ENV+3Ah: the case's difficulty level (0 Local Disturbance .. 3 Global
    // Crisis). Practice: the level picked in practice_menu.
    int gameDifficulty = 1;
    // ENV+4Eh: skill levels, 2 bits each; bits 4-5 are cryptography. Practice
    // (start_practice) sets 555h: every skill at 1.
    int skillLevels = 0x555;
    // ENV+24h: option bits; 4 = sound on, 20h = practice (exit back to FINAL).
    unsigned options = 0x24;
    // ENV+44h: the video mode (env44; CODE reads it but doesn't use it).
    int videoMode = 0;
    // randomize(): srand(time(0) & 7FFFh).
    std::uint32_t seed = 1;
    // Use this difficulty instead of the one derived below (tests, development).
    std::optional<int> difficultyOverride;

    // main(): clamp(ENV+3Ah - crypto skill + 1, -3, 5).
    int difficulty() const {
        if (difficultyOverride) return clampTo(*difficultyOverride, -3, 5);
        return clampTo(gameDifficulty - ((skillLevels >> 4) & 3) + 1, -3, 5);
    }
};

// What main() writes back.
struct CodeOutcome {
    int result = 0;          // ENV[9]: 1 decoded, 0 gave up (Esc)
    int elapsed = 0;         // ENV[15]: the clock's "minutes" (elapsed, see CodeClock)
    bool quitGame = false;   // Alt-Q confirmed: ENV[0] = FFh and exit
    unsigned options = 0;    // ENV+24h after Alt-S toggles
};

// FINAL.C compose_intercept, as start_practice runs it: here_org = random(12) + 4,
// here_city = random(16), load_world_file(random(3)), then the message: random
// org and city names for $SNDORG/$RCVORG/$SNDLOC/$RCVLOC and one each of the
// *SLOC, *RLOC, *SORG, *RORG (8 variants) and *FLUF (12) texts of TEXT.DTA in
// random order. Returns text_buf's first 512 bytes (what is copied to ENV+200h).
std::string composePracticeIntercept(const GameData& data, Rng& rng);

// The workstation clock (CLOCK.C draw_clock): `ticks` counts the 10-retrace
// waits of the input loop (7 a second); 60 ticks make one unit of `elapsed`,
// shown as elapsed / 60 : elapsed % 60 : ticks.
struct CodeClock {
    int elapsed = 0;
    int ticks = 0;
    // draw_clock's update and the text it draws ("00:00:00").
    std::string update();
};

class CodeBreak {
public:
    static constexpr int kLetters = 26;     // nletters
    static constexpr int kColumns = 42;     // message cells per row

    // main(): difficulty, randomize, make_cipher, then the bookkeeping of
    // draw_workstation (letter counts, the homophone mixing at difficulty 2 and
    // 4+, and at difficulty <= 0 the letters the computer reveals up front).
    explicit CodeBreak(const CodeSetup& setup);

    int difficulty() const { return difficulty_; }
    // The message after make_cipher: upper case, spaces (difficulty > 2) or
    // periods (1, 2) removed, last character dropped; with the homophones.
    const std::string& message() const { return message_; }
    int length() const { return static_cast<int>(message_.size()); }
    // The original message byte (plain text) at i, 0 past the end.
    unsigned char plainAt(int i) const {
        return i >= 0 && i < length() ? static_cast<unsigned char>(message_[i]) : 0;
    }
    // The byte drawn in cell i's cipher row: letters (A..'\') through the key.
    unsigned char cipherAt(int i) const;
    // The byte drawn in cell i's guess row: for letters the player's guess for
    // its cipher letter, else the byte itself.
    unsigned char guessRowAt(int i) const;

    // "CRYPTO WORKSTATION, Msg:#M173" with the last three digits from the message.
    const std::string& title() const { return title_; }

    // Cipher letter n's (0 = 'A') guess, ' ' if none.
    char guess(int n) const { return guess_[n]; }
    // Times cipher letter n occurs (after the homophone mixing).
    int count(int n) const { return count_[n]; }
    // The cipher's most frequent letter (draw_workstation's `hold`).
    int mostFrequent() const { return hold_; }
    // Whether draw_workstation shows the guesses before the first key (difficulty <= 0).
    bool guessesShownAtStart() const { return difficulty_ <= 0; }

    // A letter typed for cipher letter `ltr` (workstation_input): j = 0..25 sets
    // the guess, anything else (-1) clears it.
    void setGuess(int ltr, int j);
    // F1: one more letter revealed (computer_assist).
    void computerAssist();
    // F1's scramble_effect: the next cell it flickers (random(strlen(message))).
    int scramblePos() { return rng_.below(length()); }
    // F10: back to the letters the computer revealed (refresh_guesses).
    void refreshGuesses();
    // check_decoded: no wrong guess and good + unused / 2 > 19.
    bool decoded() const;
    // On success: every guess set to its answer.
    void solve();

    // The answer for cipher letter n (what solve() puts there).
    char answer(int n) const;

private:
    void makeCipher(const std::string& env);
    void workstationCounts();

    Rng rng_;
    int difficulty_ = 0;
    std::array<char, 30> key_{};
    std::array<char, 30> plain_{};
    std::array<char, 32> guess_{};
    std::array<int, 28> revealed_{};
    std::array<int, 28> count_{};
    int hold_ = 0;
    std::string message_;
    std::string title_;
};

}  // namespace redux
