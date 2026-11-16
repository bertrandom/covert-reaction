// GAMELIB/MSGTEXT.C (GAME's copy, MSGTXTG.C) and the text helpers of
// GAMELIB/PERSON.C: names, building types, word wrapping, garbling. See Sim.h.
#include <cstdlib>

#include "formats/Font.h"
#include "formats/TextDb.h"
#include "game/Sim.h"

namespace reaction {

namespace {

// PERSON.C first_names[female, male][64] and last_names[64]: two names per
// nationality (orgs[].nationality * 2 + 4 bits of the id).
const char* const kFirstNames[2][64] = {
    {"Georgina", "Arkada",  "Yuria",    "Anatola", "Andra",    "Nikola",  "Viktora",  "Ivana",
     "Natasha",  "Svetlana", "Anna",    "Ludmilla", "Lexandra", "Katerina", "Olga",   "Nadia",
     "Leona",    "Pat",     "Carla",    "Bobbie",  "Jenny",    "Frieda",  "Alice",    "Rose",
     "Deirdre",  "Martha",  "Lisa",     "Erica",   "Liz",      "Emma",    "Isadora",  "Jane",
     "Omara",    "Alicia",  "Hasana",   "Leila",   "Yasria",   "Ailha",   "Yousfa",   "Aziza",
     "Fatima",   "Benazir", "Ouida",    "Selima",  "Ismaila",  "Persis",  "Haurouna", "Zara",
     "Carlotta", "Josita",  "Miguela",  "Ramona",  "Luisa",    "Julia",   "Juanita",  "Raquel",
     "Isabel",   "Maria",   "Elena",    "Linda",   "Carmen",   "Agnes",   "Celeste",  "Phyllis"},
    {"Georgi",  "Arkady",  "Yuri",     "Anatol",  "Andrei",  "Nikita",   "Viktor",   "Ivan",
     "Boris",   "Nikolai", "Grigori",  "Pavel",   "Mischa",  "Petrov",   "Vladimir", "Mikhail",
     "Leon",    "Ian",     "Carl",     "Bob",     "Jim",     "Fred",     "Alan",     "Ralph",
     "Antoine", "Heinrich", "Eric",    "Gordon",  "David",   "Pierre",   "Claude",   "Gerard",
     "Omar",    "Ali",     "Hasan",    "Abdul",   "Yasr",    "Abu",      "Yousf",    "Saadi",
     "Achmet",  "Ibrahim", "Selim",    "Ismail",  "Mahmoud", "Haround",  "Suleiman", "Yezid",
     "Carlos",  "Jose",    "Miguel",   "Ramon",   "Luis",    "Julio",    "Juan",     "Pedro",
     "Enrique", "Eduardo", "Arturo",   "Jorge",   "Manuel",  "Hector",   "Franco",   "Jaime"},
};
const char* const kLastNames[64] = {
    "Katkov",  "Gorny",   "Gromyko", "Kormarov", "Molotov", "Charyk",    "Markov",   "Filatov",
    "Badenov", "Chekhov", "Gogol",   "Zhukov",   "Nevsky",  "Kemidov",   "Dubrovnik", "Kerensky",
    "Lorenz",  "Koch",    "Baader",  "Brown",    "Jones",   "Bloom",     "Cahill",   "Grivas",
    "Cook",    "Muller",  "Hansen",  "Olsen",    "Merrick", "Ballard",   "Verne",    "Konrad",
    "Abbas",   "Said",    "Daoud",   "Moussa",   "Nidal",   "Khaled",    "Hammid",   "Afafat",
    "Agha",    "Murad",   "Akbar",   "Hosayn",   "Raisuli", "Musafir",   "Nasr",     "Rashid",
    "Munoz",   "Diaz",    "Munoz",   "Perez",    "Ramirez", "Alvarez",   "Garcia",   "Lopez",
    "Santos",  "Diego",   "Cordoba", "Valdez",   "Santiago", "Enriquez", "Sanchez",  "Delarosa",
};

const char* const kSubstKeys[7] = {"$VICTIM", "$SNDORG", "$RCVORG", "$SNDLOC", "$RCVLOC", "$HLPORG", "$OBJECT"};
const char* const kClueKeywords[5] = {"$KEY", "$NAME", "$ORG", "$CITY", "$ROLE"};

}  // namespace

// ---------------------------------------------------------------- MSGTEXT.C

// lookup_text(file, key, keys, vals, n): appends the entry (or the security
// clearance line) to text_buf. keys: subst_keys, or CLUE.C's clue_keywords.
int Sim::lookupText(const char* file, const std::string& key, int nsubst, bool clueKeywords) {
    std::vector<Substitution> subst;
    for (int i = 0; i < nsubst; ++i)
        subst.emplace_back(clueKeywords ? kClueKeywords[i] : kSubstKeys[i], g_.subst[static_cast<std::size_t>(i)]);
    if (auto t = textDb(file).lookup(key, subst)) {
        g_.textBuf += *t;
        return 1;
    }
    g_.textBuf += "This information requires security clearance: ";
    g_.textBuf += key;
    return 0;
}

void Sim::lookupRandomText(const char* file, const char* key, int n) {
    n = rnd(n);
    std::string k = key;
    k[6] = static_cast<char>(n % 10 + '0');
    k[5] = static_cast<char>(n / 10 + '0');
    lookupTextSimple(file, k);
}

void Sim::makeMsgKey(int day, int msg) {
    g_.msgKey = "*MSG0000";
    if (msg < 0) {
        day = 99;
        msg = -msg / 2;
    }
    g_.msgKey[4] = static_cast<char>(g_.msgKey[4] + day / 10);
    g_.msgKey[5] = static_cast<char>(g_.msgKey[5] + day % 10);
    g_.msgKey[6] = static_cast<char>(g_.msgKey[6] + msg / 10);
    g_.msgKey[7] = static_cast<char>(g_.msgKey[7] + msg % 10);
}

// load_message_text, GAME's version: also marks the sender's organization known.
void Sim::loadMessageText(int p) {
    g_.subst[0] = career_.playerName.str();
    g_.subst[6] = career_.playerOrgName.str();
    const int sp = suspect(plot(p).suspect).person;
    const int org = person(sp).org & 15, city = person(sp).city & 15;
    g_.subst[1] = w_.orgs[static_cast<std::size_t>(org)].name.str();
    g_.subst[3] = w_.cities[static_cast<std::size_t>(city)].name.str();
    makeMsgKey(w_.hdr.msgDay, p >= 0 ? plot(p).msg : p);
    lookupTextSimple("text.dta", g_.msgKey);
    if (!tacLibrary_) w_.orgs[static_cast<std::size_t>(org)].flags |= 1;   // (#ifdef GAME_MSGTEXT)
}

void Sim::composeInterceptText(int n) {
    const Msg& m = w_.msgs[static_cast<std::size_t>(n & 127)];
    const int p = m.msg;
    g_.textBuf.clear();
    int sender = m.sender, to = m.receiver;
    int reversed = 0;   // (left unset by the original for generic messages, p < 0)
    if (p >= 0 && (plot(p).flags & 0xF00) == 0x300) {
        std::swap(sender, to);
        reversed = 1;
    }
    auto orgName = [&](int o) { return w_.orgs[static_cast<std::size_t>(o & 15)].name.str(); };
    auto cityName = [&](int c) { return w_.cities[static_cast<std::size_t>(c & 15)].name.str(); };
    g_.subst[0] = career_.playerName.str();
    g_.subst[6] = career_.playerOrgName.str();
    g_.subst[1] = orgName(person(sender).org);
    g_.subst[2] = orgName(person(to).org);
    if (!reversed) {
        g_.subst[3] = cityName(m.senderCity);
        g_.subst[4] = cityName(m.receiverCity);
    } else {
        g_.subst[4] = cityName(m.senderCity);
        g_.subst[3] = cityName(m.receiverCity);
    }
    int used = 0;
    do {
        const int r = rnd(7);
        if (used & (1 << r)) continue;
        used |= 1 << r;
        switch (r) {
        case 0:
            if (person(sender).known & 8) lookupRandomText("text.dta", "*SLOC00", 8);
            break;
        case 1:
            if (person(to).known & 8) lookupRandomText("text.dta", "*RLOC00", 8);
            break;
        case 2:
            if (person(sender).known & 4) lookupRandomText("text.dta", "*SORG00", 8);
            break;
        case 3:
            if (person(to).known & 4) lookupRandomText("text.dta", "*RORG00", 8);
            break;
        case 4:
            makeMsgKey(w_.hdr.msgDay, p >= 0 ? plot(p).msg : p);
            g_.textBuf += '\x80';
            lookupTextSimple("text.dta", g_.msgKey);
            g_.textBuf += '\x8F';
            break;
        case 5:
            lookupRandomText("text.dta", "*FLUF00", 12);
            break;
        case 6: {
            g_.msgKey = "*ALRT00";
            g_.msgKey[6] = static_cast<char>(g_.msgKey[6] + clampTo(m.level / 20, 0, 4));
            if (m.level > 15) {
                lookupTextSimple("text.dta", g_.msgKey);
            } else {
                int org = person(suspect(rnd(activeSuspects())).person).org;
                if (person(sender).org != org && person(to).org != org) {
                    g_.subst[5] = orgName(org);
                    lookupRandomText("text.dta", "*AIDD00", 2);
                }
            }
            break;
        }
        }
    } while (used != 0x7F);
}

// ---------------------------------------------------------------- PERSON.C

void Sim::appendPersonName(int p) {
    if (person(p).known & 2) {
        personName(p);
    } else {
        g_.textBuf += "Agent A";
        g_.textBuf.back() = static_cast<char>(p % 26 + 'A');
    }
}

void Sim::personName(int p) {
    const Person& pp = person(p);
    const int nat = w_.orgs[static_cast<std::size_t>(pp.org & 15)].nationality;
    const int f = nat * 2 + ((pp.id >> 1) & 15), l = nat * 2 + ((pp.id >> 5) & 15);
    g_.textBuf += (f >= 0 && f < 64) ? kFirstNames[pp.id & 1][f] : "?";
    g_.textBuf += " ";
    g_.textBuf += (l >= 0 && l < 64) ? kLastNames[l] : "?";
}

int Sim::appendBuildingType(int city, int org) {
    int level = orgAlertLevel(city, org);
    if (level == 1) g_.textBuf += " hideout";
    if (level == 2) g_.textBuf += " agent";
    if (level == 3) g_.textBuf += " safehouse";
    if (level == 4) g_.textBuf += " active cel";
    if (level == 5) g_.textBuf += " office";
    return level;
}

int Sim::orgAlertLevel(int city, int org) const {
    return Locations(const_cast<World&>(w_), rng_, career_.mm).orgAlertLevel(city & 15, org & 15);
}

bool Sim::orgCanReach(int org, int org2, int city) const {
    return Locations(const_cast<World&>(w_), rng_, career_.mm).orgCanReach(org & 15, org2 & 15, city & 15);
}

int Sim::cityMapDistance(int c1, int c2) const {
    const City& a = w_.cities[static_cast<std::size_t>(c1 & 15)];
    const City& b = w_.cities[static_cast<std::size_t>(c2 & 15)];
    int dx = std::abs(a.mapX - b.mapX), dy = std::abs(a.mapY - b.mapY);
    if (dx > dy) return dx * 2 + dy;
    return dy * 2 + dx;
}

void Sim::wrapTextBuffer(int width) {
    std::string& t = g_.textBuf;
    int pos = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '^') {
            t[i] = ' ';
            if (i + 1 < t.size()) t[i + 1] = '\n';
            else t += '\n';   // (the original writes over the terminator)
            pos = -1;
        }
        if (t[i] == '\n') pos = -1;
        ++pos;
        const char next = i + 1 < t.size() ? t[i + 1] : 0;
        if (pos > width && t[i] == ' ' && next != ' ') {
            t[i] = '\n';
            pos = 0;
        }
    }
    if (pos > 0) t += "\n";
}

void Sim::garbleMessage(unsigned mask, int n) {
    std::string& t = g_.textBuf;
    std::string tmp;
    std::size_t from = 0;
    int words = 0, on = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == ' ' || t[i] == '\n') {
            if (mask & (1u << (words % n))) {
                if (!on) from = i + 1;
                on = 1;
            } else {
                if (on) {
                    tmp += t.substr(from, i - from);
                    t[i] = ' ';   // (the original puts a blank back, also over a newline)
                    tmp += " ... ";
                }
                on = 0;
            }
            ++words;
        }
    }
    if (on && from <= t.size()) tmp += t.substr(from);
    t = tmp;
}

int Sim::drawWrappedText(int width, int y) const {
    const Font& f = fonts_->font(1);
    const std::string& t = g_.textBuf;
    const int n = static_cast<int>(t.size());
    int w = 0, lastsp = 0;
    long guard = 0;
    for (int i = 0; i < n; ++i) {
        w += f.charWidth(static_cast<std::uint8_t>(t[static_cast<std::size_t>(i)]));
        const char c = t[static_cast<std::size_t>(i)];
        if (c == ' ' || c == '\n') lastsp = i;
        if (c == '\n' || w > width * 8) {
            y += 8;
            w = 0;
            i = lastsp;
            if (y >= 0xC0) break;
            if (++guard > 100000) break;   // (a word wider than the box: the original loops)
        }
    }
    if (w > 0 && y < 0xC0) y += 8;
    return y;
}

int Sim::textWidth(const std::string& s) const { return fonts_->font(1).textWidth(s); }

}  // namespace reaction
