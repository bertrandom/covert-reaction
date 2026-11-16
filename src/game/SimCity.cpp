// The rules behind the city-loop screens: TRAVEL.C (alertness, the flight),
// BRIEFING.C mission_briefing, GAME.C main's time keeping, SCREENS.C (hotel,
// CIA intelligence section, decoded messages), SUSPECTS.C (the research office,
// the clue overview) and CITY.C (watching a building, wiretaps, break-ins,
// interrogations, capture, the prisoner exchange, coming back from the
// mini-games). Drawing and menus are left to the screens; these functions take
// the player's choice as a parameter where the original asks for it.
#include <cstdlib>
#include <map>

#include "formats/TextDb.h"
#include "game/Sim.h"

namespace redux {

namespace {

std::uint16_t shl16(int n) {
    n &= 31;
    return n < 16 ? static_cast<std::uint16_t>(1u << n) : 0;
}

constexpr std::uint8_t kBitcount[32] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
                                        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5};
constexpr int kHeadingDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kHeadingDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kHeadings[3][3] = {{7, 6, 5}, {0, 0, 4}, {1, 2, 3}};

int sign(int n) { return n > 0 ? 1 : n < 0 ? -1 : 0; }

}  // namespace

// ---------------------------------------------------------------- TRAVEL.C

void Sim::raiseAlertness(int org, int city, int amount) {
    if (amount <= 0) return;
    heat() = static_cast<std::int16_t>(heat() + amount / 2);
    for (int p = 0; p < w_.hdr.numPersons; ++p) {
        Person& pp = person(p);
        if (!pp.id || pp.fate < 0) continue;
        const int d1 = cityDistance(static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(org & 15)].home),
                                    static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(pp.org & 15)].home));
        const int d2 = cityDistance(static_cast<std::uint16_t>(w_.cities[static_cast<std::size_t>(city & 15)].code),
                                    static_cast<std::uint16_t>(w_.cities[static_cast<std::size_t>(pp.city & 15)].code));
        const int ratio = static_cast<std::int16_t>(
            (static_cast<long>(static_cast<std::int16_t>((d1 + 4) * (d2 + 2))) << 8) / (amount + 1));
        if (ratio == 0) continue;   // (a divide error in the original; amount < 2048 never gets here)
        pp.fate = static_cast<std::int16_t>(pp.fate + 2500 / ratio);
    }
}

int Sim::describeAlertLevel(int org, int who) {
    auto& tb = g_.textBuf;
    if (!orgAlertLevel(who, org)) {
        tb += "quiet";
        return 0;
    }
    const int clue = pickClue(org, who, 0, 0);
    int lvl = static_cast<std::int16_t>(
        cityDistance(static_cast<std::uint16_t>(w_.orgs[1].home),
                     static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(org & 15)].home)) *
        heat() / 24);
    if (clue != -1) lvl = (person(clue).fate + lvl) / 2;
    if (moleCities() & shl16(who)) lvl += 16;
    if (lvl < 0) lvl = 99;
    switch (lvl / 12) {
    case 0: tb += "unsuspecting"; break;
    case 1: tb += "cautious"; break;
    case 2: tb += "suspicious"; break;
    case 3: tb += "nervous"; break;
    default: tb += "very nervous";
    }
    return lvl;
}

int Sim::flyToCityFrames(int from, int to) {
    const City& a = w_.cities[static_cast<std::size_t>(from & 15)];
    const City& b = w_.cities[static_cast<std::size_t>(to & 15)];
    int x = a.mapX, y = a.mapY;
    const int tx = b.mapX, ty = b.mapY;
    int ddir = rnd(8);
    int dir = ddir;
    int alt = 0, frames = 0;
    int dx = tx - x, dy = ty - y;
    dir = kHeadings[sign(dx) + 1][sign(dy) + 1];
    long guard = 0;
    do {
        if ((ddir = kHeadings[sign(dx) + 1][sign(dy) + 1] - dir) != 0) {
            if ((ddir & 7) < 4) ++dir;
            else --dir;
            dir &= 7;
        }
        alt = clampTo((frames++ & 1) + alt, 0, (std::abs(dx) + std::abs(dy)) / 2);
        if (alt > 8) alt = 8;
        x += kHeadingDx[dir];
        y += kHeadingDy[dir];
        dx = tx - x;
        dy = ty - y;
        if (++guard > 100000) break;
    } while (dx || dy);
    return frames;
}

// travel_screen once a city other than here is picked.
void Sim::travelTo(int city) {
    const int cost = flyToCityFrames(w_.hdr.hereCity, city) / 8 * 30 + 60;
    w_.hdr.hereCity = static_cast<std::int16_t>(city);
    addChronologyEntry(4, city);
    advanceTime(cost);
    if (caseOver_) return;   // (process_events exited to FINAL)
    heat() = static_cast<std::int16_t>(heat() / 3);
}

// ---------------------------------------------------------------- BRIEFING.C / GAME.C

void Sim::missionBriefing() {
    int total = 0;
    w_.hdr.gameTime = -1;
    do {
        if (w_.hdr.numMsgs > 5) {
            int r;
            do r = rnd(16);
            while (moleCities() & shl16(r));
            total += describeNewInfo(1, r, 99, 0);
        } else {
            advanceTime(180);
        }
        g_.inBuilding = 0;
        if (caseOver_) return;
    } while ((4 - difficulty()) * 2 > total);
    if (ui_) {
        ui_->sceneVar(0, 1);
        ui_->sceneBackground("briefing.pan", 0, 0);
    }
    g_.textBuf = "The rest is up to you. ";
    if (!difficulty())
        g_.textBuf += "I've assigned one of my best research assistants to work with you on this case. ";
    g_.textBuf += "The President's counting on you Max, don't let us down.";
    wrapTextBuffer(0x28);
    notice(g_.textBuf, 8, 0x95);
}

void Sim::startNewGame() {
    w_.hdr.jailbreakPerson = -1;
    missionBriefing();
    w_.hdr.hereOrg = 1;
    w_.hdr.options = static_cast<std::int16_t>(w_.hdr.options | 0xC);
}

void Sim::resumeGame(int env15) {
    drawStatusBar();
    // advance_time(((char)ENV[15] + 29U) / 30 * 30)
    const auto m = static_cast<std::uint16_t>(static_cast<std::uint16_t>(static_cast<std::int8_t>(env15) + 29u) / 30 * 30);
    advanceTime(static_cast<std::int16_t>(m));
}

// ---------------------------------------------------------------- SCREENS.C

void Sim::hotelRest(int steps) {
    g_.inBuilding = 1;
    for (int i = 0; i < steps && !caseOver_; ++i) processEvents(60, false);
    g_.inBuilding = 0;
}

void Sim::hotelLounge() {
    g_.textBuf.clear();
    showProse("*lounge", 0x20);
    notice(g_.textBuf, 0x14, 0x50);
    for (int i = 0; i < w_.hdr.numSuspects; ++i) {
        const int p = suspect(i).person;
        if (person(p).city == w_.hdr.hereCity && !(suspect(i).flags & 0x100)) revealRandomClue(i, 0, w_.hdr.hereCity);
    }
    raiseAlertness(0, w_.hdr.hereCity, 0x14);
}

int Sim::localScan() {
    const int found = describeNewInfo(1, w_.hdr.hereCity, 100, 0);
    if (!found) {
        if (ui_) ui_->clearPlayArea();
        notice("No new information\n", 0x14, 0x29);
    }
    advanceTime(240);
    return found;
}

int Sim::internationalScan() {
    int found = 0;
    for (int k = 0; k < 4 && !caseOver_; ++k) {
        advanceTime(60);
        if (caseOver_) return found;
        found += describeNewInfo(1, rnd(16), 50, 0);
    }
    if (!found) {
        if (ui_) ui_->clearPlayArea();
        notice("No new information\n", 0x14, 0x29);
    }
    return found;
}

bool Sim::accuseDoubleAgent() {
    const int here = w_.hdr.hereCity;
    if (moleCities() & shl16(here)) {
        g_.textBuf = "The local agent confesses\nand is replaced.\n";
        g_.textBuf += "You search his files\nfor additional information...\n";
        notice(g_.textBuf, 0x14, 0x14);
        w_.hdr.doubleAgents = static_cast<std::int16_t>(moleCities() - shl16(here));
        for (int k = 0; k < 3; ++k)
            if (describeNewInfo(1, here, 100, 0)) break;
        return true;
    }
    g_.textBuf = "The local agent is checked\nand proved innocent.\nYou are barred from\nusing the local office.\n";
    notice(g_.textBuf, 0x14, 0x14);
    clearLocation(1, here);
    return false;
}

void Sim::showDecodedMessage(int n) {
    Msg& mn = w_.msgs[static_cast<std::size_t>(n & 127)];
    if (ui_) {
        ui_->teletypePaper();
        ui_->text("Message Decode", 0x78, 0x12, 5);
    }
    const int msg = mn.msg;
    const int role = plot(msg).flags & 0xFF;
    int src = mn.sender, tgt = mn.receiver;
    int who = n;   // (unset in the original when no other entry has the same time)
    if (msg >= 0) {
        if ((plot(msg).flags & 0xF00) == 0x300) std::swap(src, tgt);
        for (int j = 0; j < w_.hdr.numMsgs && j < 128; ++j) {
            Msg& mj = w_.msgs[static_cast<std::size_t>(j)];
            if (mn.time != mj.time) continue;
            for (int k = 0; k < 2 - difficulty(); ++k) {
                const unsigned prev = mj.flags;
                do mj.flags |= static_cast<std::uint16_t>(1u << rnd(4));
                while (mj.flags == prev && (prev & 0xE) != 0xE);
            }
            if (!(mj.flags & 0xE)) mj.flags |= 2;
            if (j != n) who = j;
        }
        Msg& mw = w_.msgs[static_cast<std::size_t>(who)];
        person(mn.sender).known |= static_cast<std::uint16_t>(mw.flags & 0xE);
        person(mw.sender).known |= static_cast<std::uint16_t>(mn.flags & 0xE);
        person(mn.sender).roles |= shl16(plot(msg).suspect);
        person(mn.receiver).roles |= shl16(role);
        mn.flags |= static_cast<std::uint16_t>(person(mw.sender).known & 0xE);
        mw.flags |= static_cast<std::uint16_t>(person(mn.sender).known & 0xE);
    } else {
        person(src).roles |= static_cast<std::uint16_t>(person(src).flags & 0x7FFF);
        person(tgt).roles |= static_cast<std::uint16_t>(person(tgt).flags & 0x7FFF);
    }
    if (ui_) {
        ui_->text("FROM:", 0x20, 0x30, 5);
        ui_->text("TO:", 0xA0, 0x30, 5);
    }
    if (person(src).known) suspectDossier(src, 0x20, 0x38);
    if (person(tgt).known) suspectDossier(tgt, 0xA0, 0x38);
    if (ui_) ui_->text("MESSAGE:", 0x20, 0x77, 5);
    g_.textBuf.clear();
    loadMessageText(msg);
    emitText(SimEvent::Kind::Decoded, "Message Decode", g_.textBuf, n);
    drawWrapped(0x1E, 0x20, 0x7F, 0);
    waitKey();
    mn.flags |= 0x8000;
    w_.msgs[static_cast<std::size_t>(who)].flags |= 0x8000;
}

// ---------------------------------------------------------------- SUSPECTS.C

void Sim::suspectsOverviewMarks() {
    for (int i = 0; i < w_.hdr.numSuspects; ++i) suspect(i).flags &= static_cast<std::uint16_t>(~0x4000u);
    for (auto& c : w_.clues)
        if (c.suspect != -1 && !(suspect(c.suspect).flags & 0x4000)) suspect(c.suspect).flags |= 0x4000;
}

void Sim::showPersonClues(int sus) {
    if (ui_) ui_->pageFrame("Clue", 14);
    int y = 16;
    for (int i = 0; i < 80; ++i) {
        if (clue(i).suspect != sus) continue;
        const int r = rnd(12) + 4;   // the card's indent
        if (y != 16 && ui_) ui_->cardPanel(r, y - 2);
        g_.textBuf = "\x87Source: ";
        g_.textBuf += w_.orgs[static_cast<std::size_t>(clue(i).org & 15)].name.str();
        g_.textBuf += "/";
        g_.textBuf += w_.cities[static_cast<std::size_t>(clue(i).city & 15)].name.str();
        g_.textBuf += "\n";
        clueTextWrapper(i);
        g_.textBuf += "\n";
        emit(SimEvent::Kind::Clue, {}, i);
        y = drawWrapped(32, r + 8, y, 5);
        y += 4;
    }
    y = 16;
    for (int i = 0; i < 80; ++i) {
        if (clue(i).suspect == sus && (clue(i).kind >> 8) == 1) {
            g_.textBuf.clear();
            describePerson(i);
            if (ui_) {
                ui_->portrait(g_.faceShown, 264, 16);
                ui_->sprite(108, 299, 40);
                const int fate = person(suspect(clue(i).suspect).person).fate;
                if (fate == -1) ui_->text("ARRESTED", 258, 52, 12);
                if (fate == -2) ui_->text("IN HIDING", 258, 52, 12);
                if (fate == -3) ui_->text(" TURNED", 258, 52, 12);
            }
            y += 64;
            if (y > 144) break;
        }
    }
    waitKey();
}

void Sim::researchOffice() {
    auto& tb = g_.textBuf;
    const auto& h = w_.hdr;
    if (ui_) {
        ui_->sceneBackground((h.options & 2) ? "officem.pan" : "officef.pan", 168, 0);
        ui_->sceneVar(0, (h.options & 2) ? -1 : 1);
    }
    tb = "Max, I've been\ngoing over the\ninfo we've got\nso far ...\n";
    notice(tb, 16, 17);
    constexpr unsigned kNoSelect = SimEvent::kNoSelect;
    int mmind = suspect(0).person;
    if (difficulty() || person(mmind).fate < 0 || h.hereCity != 1) mmind = -1;
    int maxa = 3;
    for (int i = 0; i < 16; ++i)
        if (maxa < orgClueCount(i)) maxa = orgClueCount(i);
    bool found = false;
    tb = "I'd look into\nthese organizations:\n";
    for (int i = 0; i < 16; ++i) {
        if (maxa - maxa / 4 <= orgClueCount(i) || (mmind != -1 && person(mmind).org == i)) {
            tb += " ";
            tb += w_.orgs[static_cast<std::size_t>(i)].name.str();
            tb += "\n";
            found = true;
        }
    }
    if (!found) tb.clear();
    maxa = 3;
    for (int i = 0; i < 16; ++i)
        if (maxa < cityClueCount(i)) maxa = cityClueCount(i);
    found = false;
    const std::size_t ln = tb.size();
    tb += "\x8FThese cities have been\n\x8Fpretty active lately:\n ";
    for (int i = 0; i < 16; ++i) {
        if (maxa - maxa / 4 <= cityClueCount(i) || (mmind != -1 && i == 1)) {
            tb += w_.cities[static_cast<std::size_t>(i)].name.str();
            tb += "\n ";
            found = true;
        }
    }
    if (!found) {
        tb.resize(ln);
        if (ln == 0) tb = "There's not\nmuch to go\nat this point.\n";
    }
    if (mmind != -1) {
        const int si = setLocationKnown(person(mmind).org, 1, 12);
        if (si == -1 || w_.sites[static_cast<std::size_t>(si)].strength == 0) {
            notice(tb, 16, 81, kNoSelect);
            if (ui_) ui_->clearPlayArea();
            tb = "The ";
            tb += w_.orgs[static_cast<std::size_t>(person(mmind).org & 15)].name.str();
            tb += " has a";
            appendBuildingType(1, person(mmind).org);
            tb += " right here in ";
            tb += w_.cities[static_cast<std::size_t>(h.hereCity & 15)].name.str();
            tb += "! I'd suggest you go there and place a wiretap or break in.";
            wrapTextBuffer(16);
            notice(tb, 16, 17);
            return;
        }
    }
    int best = -1, bscore = 0;
    for (int ev = 0; ev < h.numMsgs && ev < 128; ++ev) {
        const Msg& m = w_.msgs[static_cast<std::size_t>(ev)];
        const int msg = m.msg;
        const int k = plot(msg).flags & 0xF00;
        if (m.type == 0 && m.flags != 0 && (k == 0x200 || k == 0x300 || msg < 0) && !(m.flags & 0x8000)) {
            const int p = m.sender;
            if (!(person(p).roles & 0x7FFF)) {
                tb = "This message should give us some vital info: ";
                appendPersonName(p);
                tb += " ";
                formatMessageEvent(ev);
                tb += ". Check with the Crypto branch.";
                bscore = 6;
                best = -2;
            }
        }
    }
    for (int i = 0; i < h.numPersons; ++i) {
        const Person& p = person(i);
        if (!((p.known & 8) && (p.flags & 0x7FFF))) continue;
        if (p.fate < 0) continue;
        int sc = 0;
        if (p.known & 1) ++sc;
        if (p.known & 4) ++sc;
        if ((p.known & 8) && p.city == h.hereCity) sc += 2;
        if (p.roles & 0x7FFF) sc += 5;
        if (sc > bscore) {
            best = i;
            bscore = sc;
        }
    }
    if (best > -1) {
        suspectDossier(best, 0, 0);
        const Person& p = person(best);
        if (p.roles & 0x7FFF) tb = "Max, we've got enough evidence to arrest this agent. ";
        else tb = "Max, I suggest you check out this person. ";
        const std::string orgName = w_.orgs[static_cast<std::size_t>(p.org & 15)].name.str();
        if (p.city == h.hereCity) {
            tb += (p.id & 1) ? "He" : "She";
            tb += "'s right here in ";
            tb += w_.cities[static_cast<std::size_t>(h.hereCity & 15)].name.str();
            tb += ". ";
            if (p.known & 4) {
                if (setLocationKnown(p.org, h.hereCity, 0) != -1)
                    showProse(p.roles ? "*advicea" : "*advice1", 32, orgName);
                else
                    showProse("*advice2", 32, orgName);
            } else if (p.known & 3) {
                showProse("*advice3", 32);
            } else {
                showProse("*advice4", 32);
            }
        } else {
            showProse("*advice5", 32, w_.cities[static_cast<std::size_t>(p.city & 15)].name.str());
        }
        notice(tb, 24, 104, kNoSelect);
    }
    if (best == -2) {
        wrapTextBuffer(16);
        notice(tb, 16, 81, kNoSelect);
    }
    if (best == -1) notice(tb, 16, 81, kNoSelect);
}

// ---------------------------------------------------------------- CITY.C

// show_prose(key, width, rplc, n): a prose.dta passage appended to text_buf. The
// key is a string literal the original edits in place ("*nice0" + n), so a later
// call finds the digit already changed; the port keeps the edited keys too.
void Sim::showProse(const char* literal, int width, const std::string& rplc, int n) {
    std::string& key = proseKeys_.try_emplace(literal, literal).first->second;
    if (!key.empty() && key.back() == '0') key.back() = static_cast<char>(key.back() + n);
    lookupText("prose.dta", key, 1);
    proseSubstitute("$RPLC", rplc);
    proseSubstitute("$NAME", "Max Remington");
    proseSubstitute("$CITY", w_.cities[static_cast<std::size_t>(w_.hdr.hereCity & 15)].name.str());
    proseSubstitute("$ORG", w_.orgs[static_cast<std::size_t>(w_.hdr.hereOrg & 15)].name.str());
    proseSubstitute("$US", w_.orgs[1].name.str());
    wrapTextBuffer(width);
}

void Sim::proseSubstitute(const std::string& key, const std::string& val) {
    auto& tb = g_.textBuf;
    if (val.find(key) != std::string::npos) return;   // (the original would loop)
    for (std::size_t p; (p = tb.find(key)) != std::string::npos;) tb.replace(p, key.size(), val);
}

void Sim::ciaContactScreen(int org, int city, bool fromStreet) {
    if (!fromStreet) return;   // (last_exit != 0)
    raiseAlertness(org, city, 20);
    auto& c = w_.cities[static_cast<std::size_t>(w_.hdr.hereCity & 15)];
    c.orgsVisited |= shl16(w_.hdr.hereOrg);
    if (w_.hdr.hereOrg == 1) {
        if (ui_) {
            ui_->clearScreen();
            ui_->sceneBackground("hq.pan", 0xA8, 0);
        }
        if (!difficulty()) {
            g_.textBuf = "You arrive at\nthe CIA building\nand check with\nyour contact.\n";
            notice(g_.textBuf, 16, 17);
            researchOffice();
        }
    }
}

int Sim::cityAmbushCheck(int choice, int menuCount) {
    const int whos = person(suspect(rnd(w_.hdr.numSuspects)).person).org;
    if (!difficulty() || !(choice > 2 && choice < menuCount)) return -1;
    g_.textBuf.clear();
    if (describeAlertLevel(whos, w_.hdr.hereCity) > ((moleCities() & shl16(w_.hdr.hereCity)) ? 20 : 50)) {
        heat() = 0;
        if (ui_) ui_->clearScreen();
        g_.textBuf.clear();
        showProse("*surprise", 16);
        notice(g_.textBuf, 24, 31);
        return whos;   // ENV[12] = whos, city_exit = 10 (a car chase)
    }
    return -1;
}

Sim::WatchResult Sim::watchBuilding(int org, int city) {
    advanceTime(rnd(2) * 60 + 60);
    WatchResult r;
    if (caseOver_) {
        r.who = -1;
        return r;
    }
    int who = 0, bestOrg = org;
    long guard = 0;
    do {
        int best = 999;
        bestOrg = org;
        for (int o = 1; o < 16; ++o) {
            if (!orgAlertLevel(city, o)) continue;
            int distj = cityDistance(static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(org & 15)].home),
                                     static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(o)].home)) +
                        rnd(16);
            if (org == 0) distj = rnd(16);
            who = pickClue(o, city, 0, 0);
            if (who == -1 || person(who).flags == 0) distj += rnd(24);
            if (w_.cities[static_cast<std::size_t>(city & 15)].orgsKnown & shl16(o)) distj += rnd(16);
            if (distj < best && o != org) {
                best = distj;
                bestOrg = o;
            }
        }
        if (rnd(3) || !org) who = pickClue(bestOrg, city, 9, 0);
        else who = pickClue(org, city, 9, 0);
        if (++guard > 1000000) break;
    } while (person(who).fate < 0 || who == 0);
    g_.faceShown = person(who).id;
    if (ui_) {
        ui_->sceneBackground("binos3.pan", 150, 2);
        ui_->sceneVar(0, (person(who).id & 1) ? -1 : 1);
    }
    g_.textBuf = "You notice someone\nleaving the building.\nDo you ...\n Wait\n Follow car.\n Trace car.\n Check Data\n";
    // The menu: with a UI its answer, else the caller asks.
    r.choice = notice(g_.textBuf, 2, 20);
    r.who = who;
    r.bestOrg = bestOrg;
    return r;
}

void Sim::watchBuildingWait(int who) {
    if (person(who).known == 9) freeLocationIfUnused(who);
}

int Sim::wiretapResult(int org, int city, int score, int alarms, bool firstVisit, int& scorex) {
    if (firstVisit) {
        scorex = static_cast<std::int8_t>(score);
        raiseAlertness(w_.hdr.hereOrg, w_.hdr.hereCity, static_cast<std::int8_t>(alarms) * 50);
    }
    int site = -1;
    if (scorex) {
        site = setLocationKnown(org, city, 4);
        if (site >= 0) w_.sites[static_cast<std::size_t>(site)].strength =
            static_cast<std::int16_t>(w_.sites[static_cast<std::size_t>(site)].strength + scorex);
    }
    advanceTime(60);
    return site;
}

int Sim::successBriefing(int org, int city, const MiniGameResult& res) {
    addChronologyEntry(5, (org << 8) + city);
    const int sitei = setLocationKnown(org, city, 12);
    w_.orgs[static_cast<std::size_t>(org & 15)].flags |= 1;
    int scorex = res.scorexIn;
    if (res.lastExit) {
        scorex = static_cast<std::int8_t>(res.env8);
        raiseAlertness(org, city, static_cast<std::int8_t>(res.env9));
    }
    const int who = pickClue(org, city, 0, 0);
    if (scorex == -1) {
        raiseAlertness(org, city, 100);
        return capturedScreen(res.agreeExchange);
    }
    if ((res.env10 & 1) && who != -1) {
        for (int i = 0; i < 4; ++i) {
            if (w_.items[static_cast<std::size_t>(i)].holder == who) {
                if (ui_) {
                    ui_->sceneBackground("briefing.pan", 0, 0);
                    ui_->sceneVar(0, 2);
                }
                g_.textBuf = "Good job Max, You found the\n";
                g_.textBuf += w_.items[static_cast<std::size_t>(i)].name.str();
                g_.textBuf += "!. That should really\nslow down their operation.\n";
                notice(g_.textBuf, 16, 0x95);
                successBriefingText(i);
            }
        }
    }
    if (sitei >= 0)
        w_.sites[static_cast<std::size_t>(sitei)].strength =
            static_cast<std::int16_t>(w_.sites[static_cast<std::size_t>(sitei)].strength + scorex);
    if (res.env10 & 2) interrogateSuspect(who, res.env13 == 0);
    return 0;
}

void Sim::interrogateSuspect(int who, bool dbl) {
    auto& tb = g_.textBuf;
    const int diff = difficulty();
    w_.hdr.hereOrg = 1;
    g_.faceShown = person(who).id;
    if (ui_) {
        ui_->portraitPos(24, 37);
        ui_->sceneBackground("intrgate.pan", 0xE0, 0);
        ui_->sceneVar(0, (person(who).id & 1) ? -1 : 1);
    }
    tb = "You accuse ";
    appendPersonName(who);
    tb += "\nof taking part in\nthe plot, 'We know\nthat your role was:\n";
    if (dbl) person(who).roles |= static_cast<std::uint16_t>(person(who).flags & 0x7FFF);
    int evide = 0, roleg = -1;
    for (int i = 0; i < activeSuspects(); ++i) {
        if (person(who).roles & shl16(i)) {
            evide = 1;
            tb += suspect(i).name.str();
            tb += ".'\n";
        }
        if (suspect(i).person == who) roleg = i;
    }
    if (person(who).org != 1 && !dbl) clearLocation(person(who).org, person(who).city);
    if (!evide) {
        person(who).fate = -1;
        tb = "You have no evidence\nagainst ";
        appendPersonName(who);
        tb += (person(who).id & 1) ? ".\nHe" : ".\nShe";
        tb += " will have to\nbe released.\n";
        notice(tb, 16, 21);
        person(who).fate = 99;
        raiseAlertness(person(who).org, person(who).city, 99);
        addChronologyEntry(9, who);
        if (who == w_.hdr.jailbreakPerson) w_.hdr.jailbreakPerson = -1;
        return;
    }
    notice(tb, 16, 21);
    setLocationKnown(1, w_.hdr.hereCity, 12);
    tb.clear();
    if (!dbl) {
        person(who).fate = -1;
        showProse("*inter1", 24);
    } else {
        person(who).fate = -3;
        showProse("*doublea", 24);
    }
    addChronologyEntry(dbl ? 12 : 6, who);
    if (ui_) ui_->clearPlayArea();
    notice(tb, 16, 21);
    for (int m = 0; m < w_.hdr.numMsgs && m < 128; ++m) {
        const Msg& mm = w_.msgs[static_cast<std::size_t>(m)];
        if (!(mm.type == 0 && mm.sender == who && ((mm.flags & 0xF) || diff < 2 || person(who).fate == -3) &&
              ((plot(mm.msg).flags & 0x800) || (plot(mm.msg).flags & 0x200))))
            continue;
        tb.clear();
        showProse("*inter2", 24);
        formatMessageEvent(m);
        tb += ",\" you ask. ";
        if (kBitcount[mm.flags & 0x1F] >= diff * 2 || person(who).fate == -3) {
            tb += "\"Oh yeah, I guess I did,\" the prisoner responds.\n";
            wrapTextBuffer(24);
            if (ui_) ui_->clearPlayArea();
            notice(tb, 16, 21);
            if (ui_) ui_->toBackPage(224, 0, 96, 200);
            showDecodedMessage(m);
            if (ui_) {
                ui_->fillRect(0, 0, 224, 200, 5);
                ui_->fromBackPage(224, 0, 96, 200);
                ui_->screenWidth(224);
            }
        } else {
            tb += "\"Hmmm..., I don't seem to remember.\", the suspect responds.\n";
            wrapTextBuffer(24);
            if (ui_) ui_->clearPlayArea();
            notice(tb, 16, 21);
        }
    }
    person(who).known |= 0x71F;
    if (dbl || person(who).roles) {
        const bool mastermind = suspect(roleg).flags & 0x100;
        if (mastermind) {
            tb = "The MasterMind breaks down and lists the participants in the plot.";
            wrapTextBuffer(24);
            if (ui_) ui_->clearPlayArea();
            notice(tb, 16, 21);
        }
        for (int i = 0; i < activeSuspects(); ++i) {
            if (suspect(i).person == who) suspect(i).found |= 0x1F;
            if (suspect(roleg).flags & 0x100) {
                const int pidd = suspect(i).person;
                person(pidd).known |= 0x1F;
                person(pidd).roles |= static_cast<std::uint16_t>(person(pidd).flags & 0x7FFF);
                suspectDossier(pidd, 0, 0);
                waitKey();
            }
        }
        if (!dbl) {
            if (ui_) {
                ui_->clearScreen();
                ui_->portraitPos(101, 19);
            }
            g_.faceShown = person(who).id;
            if (ui_) {
                ui_->sceneBackground("injail.pan", 0xA8, 0);
                ui_->sceneVar(0, (person(who).id & 1) ? -1 : 1);
            }
            tb.clear();
            appendPersonName(who);
            tb += "\nis taken to\nthe local jail.\n";
            notice(tb, 24, 21);
        }
        if (ui_) {
            ui_->clearScreen();
            ui_->sceneBackground("briefing.pan", 0, 0);
            ui_->sceneVar(0, 2);
        }
        tb.clear();
        appendPersonName(who);
        const std::string name = tb;
        tb.clear();
        showProse("*nice0", 36, name, (suspect(roleg).rank - 1) / 3);
        wrapTextBuffer(40);
        notice(tb, 16, 0x95);
    }
    processEvents(240, true);
}

void Sim::successBriefingText(int n) {
    const unsigned mask = 1u << n;
    int cell = -1;
    for (int i = 0; i < w_.hdr.numMsgs && i < 128; ++i) {
        if (w_.msgs[static_cast<std::size_t>(i)].type != 0) continue;
        const int pidx = w_.msgs[static_cast<std::size_t>(i)].msg;
        if (plot(pidx).items & mask) cell = plot(pidx).suspect;
        if (cell != -1 && plot(pidx).suspect == cell)
            for (int j = 0; j < w_.hdr.numPlots; ++j)
                if (plot(pidx).msg == plot(j).msg) plot(j).state = 999;
    }
    addChronologyEntry(7, n);
    w_.items[static_cast<std::size_t>(n & 3)].holder = -1;
}

int Sim::capturedScreen(bool agree) {
    if (ui_) ui_->sceneBackground("surndr01.pan", 0xE0, 0);
    g_.textBuf.clear();
    showProse("*grilled", 0x18);
    if (!prisonerExchange(agree)) {
        for (int i = 0; !caseOver_ && i < rnd(3) + 2; ++i) {
            advanceTime(360);
            const int jb = w_.hdr.jailbreakPerson;
            if (jb != -1) {
                if (ui_) {
                    ui_->clearScreen();
                    ui_->sceneBackground("warning.pan", 0xA0, 0);
                }
                auto& tb = g_.textBuf;
                tb = "JailBreak in ";
                tb += w_.cities[static_cast<std::size_t>(person(jb).city & 15)].name.str();
                tb += "!\n";
                tb += w_.orgs[static_cast<std::size_t>(person(jb).org & 15)].name.str();
                tb += " agents\nfree ";
                appendPersonName(jb);
                tb += "!\n";
                notice(tb, 0x10, 0x15);
                person(jb).known |= 0x40;
                person(jb).fate = 99;
                clearLocation(person(jb).org, person(jb).city);
                addChronologyEntry(10, jb);
                w_.hdr.jailbreakPerson = -1;
            }
        }
        if (caseOver_) return 0;
        if (ui_) ui_->sceneBackground("escape.pan", 0xE0, 0);
        g_.textBuf.clear();
        showProse("*escape", 0x18);
        notice(g_.textBuf, 0x10, 0x15);
        return 7;   // city_exit = 7: escape (TAC)
    }
    if (difficulty()) clearLocation(w_.hdr.hereOrg, w_.hdr.hereCity);
    return 0;       // left_building = 1
}

bool Sim::prisonerExchange(bool agree) {
    int p = -1, worth = 99;
    for (int i = 0; i < activeSuspects(); ++i) {
        const int who = suspect(i).person;
        int rank = suspect(i).importance;
        for (int idx = 0; idx < w_.hdr.numPlots - 1; ++idx)
            if (plot(idx).suspect == i && plot(idx).state == 999) rank -= 2;
        if (person(who).fate == -1 && rank <= worth) {
            p = who;
            worth = rank;
        }
    }
    g_.textBuf += " No thanks, Squinty.\n Agree to exchange.\n";
    const int choice = notice(g_.textBuf, 0x10, 0x15);
    if (ui_) agree = choice == 1;   // (with a UI the player answers the menu)
    if (!agree) return false;
    g_.textBuf = "You are exchanged\nfor ";
    if (p != -1) {
        appendPersonName(p);
        g_.textBuf += ".\n";
        person(p).fate = 99;
        person(p).known |= 0x40;
        clearLocation(person(p).org, person(p).city);
        addChronologyEntry(13, p);
    } else {
        g_.textBuf += "two double agents.\n";
        std::uint16_t m = moleCities();
        m |= shl16(rnd(16));
        m |= shl16(rnd(16));
        w_.hdr.doubleAgents = static_cast<std::int16_t>(m);
    }
    notice(g_.textBuf, 0x10, 0x79);
    return true;
}

}  // namespace redux
