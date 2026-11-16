#include "game/NewChar.h"

#include <algorithm>

#include "game/Face.h"
#include "game/Paper.h"

namespace redux {

// clang-format off
const std::array<PlotDef, 16> kPlotDefs = {{
    {"Welcome Max", 0xF00, {{1, -1, -1, -1, -1, -1, -1, -1}},
     {{{{"the US Embassy", "a top secret code book"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"'KeyHole' Sattelite", 0x900, {{0, 6, 4, -1, -1, -1, -1, 1}},
     {{{{"sattelite expert Xavier Rayban", ""}}, {{"the uplink ground station", ""}}, {{"CIA storage", "X-Band radio transmitter"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Freedom Games", 0xC00, {{7, 2, 6, -1, -1, -1, -1, 1}},
     {{{{"the Freedom Games security plan", ""}}, {{"an Israeli security expert", ""}}, {{"the Israeli athletic dormitory", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"SuperDrug", 0x600, {{0, 4, 3, -1, -1, -1, -1, 1}},
     {{{{"the noted chemist Dr Pasture", ""}}, {{"a large industrial warehouse", "chemicals"}}, {{"new super drug", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Liberian Election", 0x900, {{11, 6, 8, -1, -1, -1, -1, 1}},
     {{{{"Geraldo Corazon", ""}}, {{"the Election Commission HQ", ""}}, {{"the Social Democrat campaign rally", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Summit Conference", 0xC00, {{10, 9, 2, -1, -1, -1, -1, 1}},
     {{{{"Carlos the Hyena", ""}}, {{"summit ID Documents", ""}}, {{"a leading Head of State", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Rampant Inflation", 0xA00, {{10, 4, 12, -1, -1, -1, -1, 1}},
     {{{{"Willie the Pen", ""}}, {{"the Treasury Building", "Currency Plates"}}, {{"medium denomination currency", "currency paper"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Stolen Nuke", 0xC00, {{7, 9, 4, -1, -1, -1, -1, 1}},
     {{{{"AirBase Passwords", ""}}, {{"AirBase ID Cards", ""}}, {{"an AirBase", "a Nuclear Bomb"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Super Cannon", 0xB00, {{4, 4, 12, -1, -1, -1, -1, 1}},
     {{{{"a chemical plant", "explosive propellants"}}, {{"a metal foundry", "high-strength tubes"}}, {{"the super cannon", "precision lathe"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Build Nuclear Weapon", 0xD00, {{4, 0, 12, -1, -1, -1, -1, 1}},
     {{{{"a breeder reactor", "plutonium 235"}}, {{"physicist Dr A. Einstone", ""}}, {{"a nuclear bomb", "imploding detonator"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Terror Campaign", 0xC00, {{6, 8, 2, -1, -1, -1, -1, -1}},
     {{{{"train station", ""}}, {{"the Veteran's Day parade", ""}}, {{"a Supreme Court Justice", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Virus X", 0xC00, {{0, 4, 12, -1, -1, -1, -1, -1}},
     {{{{"bacteriologist Edgar Coli", ""}}, {{"the medical college", "virulent virus samples"}}, {{"deadly virus X", "one million Petri dishes"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Drug Shortage", 0xA00, {{6, 2, 3, -1, -1, -1, -1, -1}},
     {{{{"InterPol Headquarters", ""}}, {{"chief drug investigator", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Train Wreck", 0xC00, {{7, 5, 6, -1, -1, -1, -1, -1}},
     {{{{"VIP travel itinerary", ""}}, {{"the locomotive plant", "train schematics"}}, {{"the train station", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Prison Break", 0x700, {{9, 11, 10, -1, -1, -1, -1, -1}},
     {{{{"prison visitor pass", ""}}, {{"the prison warden", ""}}, {{"Manny Noriega", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
    {"Stealth Fighter", 0x300, {{11, 7, 4, -1, -1, -1, -1, -1}},
     {{{{"fighter pilot", ""}}, {{"airbase operation plans", ""}}, {{"the secret hangar", "stealth fighter"}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}, {{"", ""}}}}},
}};
// clang-format on

const std::array<const char*, 13> kCrimeNames = {
    "Kidnapping", "Theft",   "Assassination",    "Drug Smuggle", "Theft",      "Photo",      "Bombing",
    "DataCrime",  "Terrorist Attack", "Forgery", "Jail Break",   "Blackmail",  "Manufacture",
};

const std::array<const char*, 26> kMastermindNames = {
    "PFO",       "M18",           "Jamaican Gang", "Haitian Junta", "Red September", "Iraqi SP",
    "Muslim Jihad", "Shining Way", "Tupamaros",    "Colombian Cartel", "Mafia",      "Libyan Emb.",
    "FLN",       "Amazon Cartel", "Mercenaries",   "Death Squads",  "Rev. Guards",   "UnionCorsique",
    "PIFA",      "Red Army",      "Red Battalion", "Direct Action", "Stassi",        "PRC",
    "Marxists",  "Dignity Bttn",
};

const std::array<const char*, 12> kMonthNames = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

const std::array<const char*, 5> kSkillNames = {"Combat", "Driving", "Cryptography", "Electronics", "Stamina"};

const std::array<std::uint8_t, 4> kRatingColors = {4, 9, 8, 2};

std::string trainingMenu(bool practice) {
    std::string s = " ";
    for (int i = 0; i < 4; ++i) {
        s += kSkillNames[static_cast<std::size_t>(i)];
        s += practice ? " practice\n " : " training\n ";
    }
    return s;
}

const char* ratingName(int level) {
    static const char* names[] = {"Average", "Good", "Excellent", "Awesome"};
    return level >= 0 && level < 4 ? names[level] : "";
}

void startNewCharacter(GameState& g) {
    auto& c = g.career;
    c.careerPoints = 0;
    c.curMonth = 0;
    c.history[0] = 0xFF;
    c.mastermindsArrested = 0;
    c.careerRank = 99;
}

void chooseGender(GameState& g, int menuResult) {
    if (menuResult == 1) g.world.hdr.options = static_cast<std::int16_t>(g.world.hdr.options | 2);
}

void chooseDifficulty(GameState& g, int menuResult) {
    g.world.hdr.difficulty = static_cast<std::int16_t>(std::clamp(menuResult, 0, 3));
}

bool CodenameField::key(int k) {
    auto& t = buf_;
    const auto len = static_cast<std::size_t>(len_);
    const auto cur = static_cast<std::size_t>(cursor_);
    if (k == 13) return true;
    if (k == 0x4B00 && cursor_) --cursor_;
    if (k == 0x4D00 && len_ - 1 > cursor_) ++cursor_;
    if (k == 0x5200) {  // Insert: a space at the cursor, the last character drops off
        t.insert(cur, 1, ' ');
        t.resize(len);
    }
    if (k == 0x5300) {  // Delete
        t.erase(cur, 1);
        t.resize(len, ' ');
        t[cur] = ' ';    // the original blanks the character pulled under the cursor too
        t[len - 1] = ' ';
    }
    if (k == 8 && cursor_) {
        t.erase(cur - 1, 1);
        t.resize(len, ' ');
        --cursor_;
    }
    if (k < 0x80 && k >= ' ') {
        if (first_) t.assign(len, ' ');
        t[static_cast<std::size_t>(cursor_)] = static_cast<char>(k);
        if (len_ - 1 > cursor_) ++cursor_;
    }
    first_ = false;
    return false;
}

std::string CodenameField::result() const {
    std::string t = buf_;
    std::size_t i = static_cast<std::size_t>(len_ - 1);
    for (; i > 0 && t[i] == ' '; --i) t.resize(i);
    return t;
}

void finishTraining(GameState& g, std::array<int, 4>& lvl, int total, std::array<int, 4>* shown) {
    int i = 0;
    for (; i < 4 - total; ++i)
        if (lvl[static_cast<std::size_t>(i)] < 3) ++lvl[static_cast<std::size_t>(i)];
    if (shown) *shown = lvl;
    // training_rating(lvl), then `if (lvl[i] == 2) lvl[i] = 1;` with i left at
    // 4 - total by the loop above: after picking all four points the first skill
    // loses Excellent. (i == 4, nothing picked, is past the array in the original:
    // a stack word no one reads afterwards.)
    if (i < 4 && lvl[static_cast<std::size_t>(i)] == 2) lvl[static_cast<std::size_t>(i)] = 1;
    auto& sk = g.world.hdr.skillLevels;
    // skill_levels += ...: added to whatever it held (0 on a fresh start).
    for (i = 0; i < 4; ++i) sk = static_cast<std::int16_t>(sk + (lvl[static_cast<std::size_t>(i)] << (i << 1)));
    if (!g.world.hdr.difficulty) sk = 0x55;
    sk = static_cast<std::int16_t>(sk | 0x500);
}

std::string choosePlot(GameState& g, const GameData& data) {
    auto& c = g.career;
    auto& w = g.world;
    Rng& rng = g.rng;
    for (int i = 0; i < 16; ++i) rng.below(0xFFF);
    if (w.hdr.difficulty || c.curMonth) {
        const int last = c.plotNum;
        do c.plotNum = static_cast<std::int16_t>(rng.below(15) + 1);
        while (last == c.plotNum);
    } else {
        c.plotNum = 0;
    }
    auto orgFlags = [&] { return w.orgs[static_cast<std::size_t>(c.mm.org)].flags; };
    // (1L << flags) & masterminds_arrested: MSC's long shift takes the count
    // from CL, so the leader code (low byte); FFFFh shifts the bit out.
    auto arrested = [&] {
        const unsigned n = orgFlags() & 0xFF;
        return n < 32 && (c.mastermindsArrested >> n & 1);
    };
    do {
        int k = 0;
        do {
            if (!(k & 7)) {
                c.worldNum = static_cast<std::int16_t>(rng.below(3));
                w.loadWorld(data, c.worldNum);
            }
            c.mm.known = 0;
            c.plotEpisodes = 0;
            c.mm.org = static_cast<std::int16_t>(rng.below(12) + 4);
            c.mm.city = static_cast<std::int16_t>(rng.below(16));
            c.lastEpisode = -1;
        } while ((++k < 999 && arrested()) || orgFlags() == 0xFFFF);
        c.mm.id = static_cast<std::int16_t>(faceCodeFor(orgFlags() & 0xFF));
        c.leaderCode = static_cast<std::int16_t>(orgFlags() & 0xFF);
    } while (!(kPlotDefs[static_cast<std::size_t>(c.plotNum)].orgs & orgFlags()));
    std::string key = "*PL0090";
    key[3] = static_cast<char>(c.plotNum / 10 + '0');
    key[4] = static_cast<char>(c.plotNum % 10 + '0');
    return key;
}

std::vector<std::string> storyPages(const TextDb& plot, std::string key) {
    std::vector<std::string> pages;
    while (auto text = plot.lookup(key)) {
        wrapTextBuffer(*text, 40);
        pages.push_back(*text);
        ++key[6];
    }
    return pages;
}

EpisodeChoice chooseEpisode(GameState& g) {
    auto& c = g.career;
    const PlotDef& pd = kPlotDefs[static_cast<std::size_t>(c.plotNum)];
    int tries = 0, episode = 0;
    bool usable = false;
    do {
        episode = g.rng.below(6);
        const auto e = static_cast<std::size_t>(episode);
        usable = !((c.plotEpisodes & (1 << episode)) || pd.crime[e] == -1);
        if (pd.crime[e + 1] == -1) {
            const int m = 0xFF >> (8 - episode);
            if ((m & c.plotEpisodes) != m) usable = false;
        }
        if (episode == c.lastEpisode && ++tries < 99) usable = false;
    } while (!usable && ++tries < 999);
    EpisodeChoice r;
    if (tries >= 999) return r;
    r.ok = true;
    const auto e = static_cast<std::size_t>(episode);
    c.lastEpisode = static_cast<std::int16_t>(episode);
    c.episodeCrime = static_cast<std::int16_t>(pd.crime[e]);
    if (episode < 7) {
        c.playerName.set(pd.roles[e][0]);
        c.playerOrgName.set(pd.roles[e][1]);
    } else {
        c.playerName.set("Max Remington's apartment");
        c.playerOrgName.set("a mattress full of cash");
    }
    r.storyKey = (((1 << episode) & 3) | c.plotEpisodes) == 3 ? "*PL000A" : "*PL000a";
    r.storyKey[3] = static_cast<char>(c.plotNum / 10 + '0');
    r.storyKey[4] = static_cast<char>(c.plotNum % 10 + '0');
    r.storyKey[5] = static_cast<char>(episode + '0');
    r.showStory = !g.briefed || episode == 7;
    return r;
}

void startCaseClock(GameState& g) {
    auto& h = g.world.hdr;
    h.numMsgs = h.clockBase = h.gameTime = h.numSites = h.seenOrgs = 0;
}

CaseSetup caseSetupFor(const GameState& g) {
    CaseSetup s;
    s.crime = g.career.episodeCrime;
    s.world = g.career.worldNum;
    s.difficulty = g.world.hdr.difficulty;
    s.mm = g.career.mm;
    s.curMonth = g.career.curMonth;
    return s;
}

}  // namespace redux
