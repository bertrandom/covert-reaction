#include "game/CodeBreak.h"

#include <vector>

#include "formats/TextDb.h"
#include "game/World.h"

namespace redux {

namespace {

bool isUpper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
// The range draw_workstation / draw_message send through the key: 'A'..'\'.
bool isKeyed(unsigned char c) { return c >= 'A' && c <= '\\'; }

}  // namespace

std::string composePracticeIntercept(const GameData& data, Rng& rng) {
    // start_practice
    rng.below(12);                    // here_org = random(12) + 4
    rng.below(16);                    // here_city = random(16)
    World world;
    world.loadWorld(data, rng.below(3));
    const TextDb db = TextDb::load(data, "text.dta");

    // MSGTEXT.C's subst_vals start out as blanks (51 or 25 spaces).
    std::vector<Substitution> subst = {
        {"$VICTIM", std::string(51, ' ')}, {"$SNDORG", std::string(25, ' ')},
        {"$RCVORG", std::string(25, ' ')}, {"$SNDLOC", std::string(25, ' ')},
        {"$RCVLOC", std::string(25, ' ')}, {"$HLPORG", std::string(25, ' ')},
        {"$OBJECT", std::string(51, ' ')},
    };
    // compose_intercept
    subst[1].second = world.orgs[static_cast<std::size_t>(rng.below(12) + 4)].name.str();
    subst[2].second = world.orgs[static_cast<std::size_t>(rng.below(12) + 4)].name.str();
    subst[3].second = world.cities[static_cast<std::size_t>(rng.below(16))].name.str();
    subst[4].second = world.cities[static_cast<std::size_t>(rng.below(16))].name.str();

    std::string text;
    auto lookupRandom = [&](std::string key, int n) {   // lookup_random_text
        n = rng.below(n);
        key[6] = static_cast<char>(n % 10 + '0');
        key[5] = static_cast<char>(n / 10 + '0');
        if (auto t = db.lookup(key, subst)) text += *t;
        else text += "This information requires security clearance: " + key;
    };
    static const char* const kKeys[5] = {"*SLOC00", "*RLOC00", "*SORG00", "*RORG00", "*FLUF00"};
    static const int kVariants[5] = {8, 8, 8, 8, 12};
    unsigned bits = 0;
    do {
        int kind = rng.below(5);
        if (!(bits & 1u << kind)) {
            bits |= 1u << kind;
            lookupRandom(kKeys[kind], kVariants[kind]);
        }
    } while (bits != 0x1F);
    // ENV[0x200 + idx] = text_buf[idx] for 512 bytes
    if (text.size() > 0x200) text.resize(0x200);
    return text;
}

std::string CodeClock::update() {
    elapsed += ticks / 60;
    ticks %= 60;
    std::string s = "00:00:00";
    int mins = elapsed / 60;
    s[0] = static_cast<char>(mins / 10 + '0');
    s[1] = static_cast<char>(mins % 10 + '0');
    int sec = elapsed % 60;
    s[3] = static_cast<char>(sec / 10 + '0');
    s[4] = static_cast<char>(sec % 10 + '0');
    s[6] = static_cast<char>(ticks / 10 + '0');
    s[7] = static_cast<char>(ticks % 10 + '0');
    return s;
}

CodeBreak::CodeBreak(const CodeSetup& setup) : rng_(setup.seed), difficulty_(setup.difficulty()) {
    makeCipher(setup.message);
    workstationCounts();
}

void CodeBreak::makeCipher(const std::string& env) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\";
    key_.fill(0);
    plain_.fill(0);
    for (std::size_t i = 0; i < sizeof kAlphabet - 1; ++i) key_[i] = plain_[i] = kAlphabet[i];
    guess_.fill(' ');
    guess_[29] = guess_[30] = guess_[31] = 0;

    for (int i = 0; i < kLetters; ++i) {
        int j = rng_.below(kLetters);
        std::swap(key_[static_cast<std::size_t>(i)], key_[static_cast<std::size_t>(j)]);
        revealed_[static_cast<std::size_t>(i)] = 0;
    }
    // message[i] = ENV[i + 0x200] for 512 bytes, read as a C string
    message_ = env.substr(0, std::min<std::size_t>(env.size(), 0x200));
    if (auto nul = message_.find('\0'); nul != std::string::npos) message_.resize(nul);
    for (std::size_t i = 0; i < message_.size(); ++i) {
        if (message_[i] == ' ' && difficulty_ > 2) message_.erase(i, 1);
        if (i < message_.size() && message_[i] == '.' && (difficulty_ == 1 || difficulty_ == 2))
            message_.erase(i, 1);
    }
    if (!message_.empty()) message_.pop_back();      // the space lookup_text ends with
    for (auto& c : message_)                          // strupr
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 0x20);
}

void CodeBreak::workstationCounts() {
    auto at = [](auto& a, int i) -> auto& { return a[static_cast<std::size_t>(i)]; };
    const int len = length();
    // The title's message number is read before the homophone mixing changes
    // the message (draw_workstation draws it first).
    title_ = "CRYPTO WORKSTATION, Msg:#M173";
    title_[26] = static_cast<char>((plainAt(13) & 7) + '0');
    title_[27] = static_cast<char>((plainAt(21) & 7) + '1');
    title_[28] = static_cast<char>((plainAt(7) & 7) + '2');
    for (int i = 0; i < len; ++i) {
        unsigned char ch = plainAt(i);
        if (isUpper(ch)) at(count_, key_[ch - 'A'] - 'A')++;
    }
    // Difficulty 2 and 4+: every cipher letter the message doesn't use takes over
    // the odd-position occurrences of the most frequent one's plain letter.
    if (difficulty_ == 2 || difficulty_ > 3) {
        int hold = 0, cur = 0;
        for (int i = 0; i < kLetters; ++i) {
            if (at(count_, i) != 0) continue;
            int max = 0;
            for (int k = 0; k < kLetters; ++k)
                if (at(count_, k) > max) { hold = k; max = at(count_, k); }
            char ch = 0;
            for (int k = 0; k < kLetters; ++k)
                if (at(key_, k) == hold + 'A') { ch = at(plain_, k); break; }
            for (int k = 0; k < kLetters; ++k)
                if (at(key_, k) == i + 'A') { cur = k; break; }
            for (int k = 0; k < len; ++k) {
                if (message_[static_cast<std::size_t>(k)] == ch && (k & 1)) {
                    message_[static_cast<std::size_t>(k)] = static_cast<char>(cur + 'A');
                    at(count_, i)++;
                    at(count_, hold)--;
                    at(plain_, cur) = ch;
                }
            }
        }
    }
    int max = 0;
    for (int i = 0; i < kLetters; ++i) {
        if (at(count_, i) > max) { max = at(count_, i); hold_ = i; }
        at(count_, i) = 0;
    }
    // Recounted while the message is typed out (the left column's numbers).
    for (int i = 0; i < len; ++i) {
        unsigned char ch = plainAt(i);
        if (isKeyed(ch)) at(count_, key_[ch - 'A'] - 'A')++;
    }
    if (difficulty_ <= 0) {
        for (int i = 0; i < kLetters; ++i)
            if (at(key_, i) == hold_ + 'A') {
                at(guess_, hold_) = static_cast<char>(i + 'A');
                at(revealed_, hold_) = 1;
            }
        for (int k = difficulty_; k < 0; ++k) computerAssist();
    }
}

unsigned char CodeBreak::cipherAt(int i) const {
    unsigned char ch = plainAt(i);
    if (isKeyed(ch)) ch = static_cast<unsigned char>(key_[ch - 'A']);
    return ch;
}

unsigned char CodeBreak::guessRowAt(int i) const {
    unsigned char ch = cipherAt(i);
    if (isKeyed(plainAt(i))) ch = static_cast<unsigned char>(guess_[ch - 'A']);
    return ch;
}

void CodeBreak::setGuess(int ltr, int j) {
    guess_[static_cast<std::size_t>(ltr)] = (j < 0 || j >= kLetters) ? ' ' : static_cast<char>(j + 'A');
}

void CodeBreak::computerAssist() {
    bool any = false;
    for (int i = 0; i < kLetters; ++i) any = any || count_[static_cast<std::size_t>(i)] != 0;
    if (!any) return;      // the original would loop forever
    int tries = 0, n = 0;
    do {
        do n = rng_.below(kLetters);
        while (count_[static_cast<std::size_t>(n)] == 0);
    } while (guess_[static_cast<std::size_t>(n)] != ' ' && ++tries < 999);
    for (int i = 0; i < kLetters; ++i)
        if (key_[static_cast<std::size_t>(i)] == n + 'A') {
            guess_[static_cast<std::size_t>(n)] = plain_[static_cast<std::size_t>(i)];
            revealed_[static_cast<std::size_t>(n)] = 1;
        }
}

void CodeBreak::refreshGuesses() {
    for (int i = 0; i < kLetters; ++i) {
        auto& g = guess_[static_cast<std::size_t>(i)];
        if (revealed_[static_cast<std::size_t>(i)]) {
            for (int j = 0; j < kLetters; ++j)
                if (key_[static_cast<std::size_t>(j)] == i + 'A') g = plain_[static_cast<std::size_t>(j)];
        } else {
            g = ' ';
        }
    }
}

bool CodeBreak::decoded() const {
    int good = 0, wrong = 0, unused = 0;
    for (int i = 0; i < kLetters; ++i) {
        int k = key_[static_cast<std::size_t>(i)] - 'A';
        const char g = guess_[static_cast<std::size_t>(k)];
        const int c = count_[static_cast<std::size_t>(k)];
        if (g != ' ' && c > 0) {
            if (g == plain_[static_cast<std::size_t>(i)]) ++good;
            else ++wrong;
        }
        if (c == 0) ++unused;
    }
    return unused / 2 + good > 19 && !wrong;
}

void CodeBreak::solve() {
    for (int i = 0; i < kLetters; ++i)
        guess_[static_cast<std::size_t>(key_[static_cast<std::size_t>(i)] - 'A')] = plain_[static_cast<std::size_t>(i)];
}

char CodeBreak::answer(int n) const {
    for (int i = 0; i < kLetters; ++i)
        if (key_[static_cast<std::size_t>(i)] == n + 'A') return plain_[static_cast<std::size_t>(i)];
    return ' ';
}

}  // namespace redux
