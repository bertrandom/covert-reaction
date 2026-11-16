// GAMELIB/CLUE.C: new information, clues, the dossier and the message events.
// See Sim.h.
#include <stdexcept>

#include "formats/TextDb.h"
#include "game/Sim.h"

namespace redux {

namespace {

std::uint16_t shl16(int n) {
    n &= 31;
    return n < 16 ? static_cast<std::uint16_t>(1u << n) : 0;
}

const char* const kClueKindNames[40] = {
    " tied to ",       " registered to ",  " acquired by ",    " delivered to ",   " requested for ",
    " tied to ",       " registered to ",  " traced to ",      " smuggled into ",  " requested for ",
    " tied to ",       " rented by ",      " frequented by ",  " is in ",          " linked to ",
    " purchased by ",  " purchased by ",   " traced to ",      " purchased in ",   " linked to ",
    " received by ",   " signed for by ",  " traced to ",      " sent to ",        " refers to ",
    " withdrawn by ",  " withdrawn by ",   " paid by ",        " transaction in ", " requested for ",
    " withdrawn by ",  " withdrawn by ",   " paid by ",        " transaction in ", " requested for ",
    " assigned to ",   " assigned to ",    " assigned to ",    " used in ",        " assigned to ",
};
constexpr std::uint8_t kBitcount[32] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
                                        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5};
const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kRankNames[8] = {"Recruit",   "Operative",     "Technician",   "Agent",
                                   "Organizer", "Special Agent", "Group Leader", "MasterMind"};
const char* const kNumberPrefix[8][8] = {
    {"Ford Escort #", "Chevy Nova #", "Toyota Tercel #", "VW Golf #", "Mazda RX7 #", "Honda Civic #", "Peugot #",
     "Chrysler #"},
    {"Walther PPK #", "Smith&Wesson 9mm #", "Berreta M92S #", "Browning 9mm #", "Hechler&Koch VP #", "Luger 9mm #",
     "Colt 45 #", "357 Magnum #"},
    {"Rue Laverne ", "HochStrasse ", "Main St. ", "Webley Ct. ", "Holmley Park ", "ObensGrabbe ", "La Paz Blvd. ",
     "North Ave. "},
    {"PanAm #", "United #", "Sabena #", "Lufthansa #", "El Al #", "KLM #", "SwissAir #", "BOAC #"},
    {"W.Union #", "RCA #", "Telex #", "AmTel #", "InterTel #", "SatComm #", "TelTron #", "ComLink #"},
    {"$", "$", "$", "$", "$", "$", "$", "$"},
    {"$", "$", "$", "$", "$", "$", "$", "$"},
    {"Swiss Passport #", "InterPol ID#", "Bahamian Passport #", "Gold Card #", "Swedish Passport #", "Green Card #",
     "VISTA Card #", "Liberian Passport #"},
};
const char* const kClueMethodNames[7] = {"Clandestine Photo",  "Electronic WireTap", "Covert Surveillance",
                                         "File Record Search", "Local Informant",    "INTERPOL Data Base",
                                         "Local Authorities"};

std::string itoa10(int v) { return std::to_string(v); }

}  // namespace

// ---------------------------------------------------------------- locations

int Sim::addLocation(int org, int city, unsigned flags) {
    return loc().addLocation(org, city, static_cast<std::uint16_t>(flags));
}
int Sim::setLocationKnown(int org, int city, unsigned flags) {
    return loc().setLocationKnown(org, city & 15, static_cast<std::uint16_t>(flags));
}
void Sim::clearLocation(int org, int city) { loc().clearLocation(org, city & 15); }
void Sim::freeLocationIfUnused(int i) {
    Person& p = person(i);
    if (p.flags & 0x7FFF) return;
    p.id = 0;
    p.org = -1;
    p.city = -1;
    p.known = 0;
}

// pick_clue(org, city, known, show): the person slot of (org, city), ORing
// `known` into it (-1 if that leaves it 0); else a new person in a random free
// slot (slot 0 for the MasterMind's place), unless known is 0. show: describe_clue.
int Sim::pickClue(int org, int city, unsigned known, int show) {
    const int np = w_.hdr.numPersons;
    for (int i = 0; i < np; ++i) {
        Person& p = person(i);
        if (p.org == org && p.city == city) {
            const unsigned old = p.known;
            p.known = static_cast<std::uint16_t>(p.known | known);
            if (p.known) {
                if (show) describeClue(i, old);
                return i;
            }
            return -1;
        }
    }
    int i = 0;
    long guard = 0;
    do {
        i = rnd(63) + 1;
        if (++guard > 10000000) throw std::runtime_error("pick_clue: no free person slot (the original hangs)");
    } while (person(i).id);
    if (org == career_.mm.org && city == career_.mm.city) {
        i = 0;
        known |= career_.mm.known;
    }
    known &= 0xFFFF;
    if (!known) return -1;
    Person& p = person(i);
    const unsigned old = p.known;
    p.org = static_cast<std::int16_t>(org);
    p.city = static_cast<std::int16_t>(city);
    p.known = static_cast<std::uint16_t>(known);
    p.flags = 0;
    p.roles = 0;
    bool ok;
    do {
        ok = true;
        p.id = static_cast<std::int16_t>(rnd(0x7FFF));
        // "persons[i].id & 0x3f == persons[j].id & 0x3f" parses as
        // id & (0x3f == persons[j].id) & 0x3f
        for (int j = 0; j < np; ++j)
            if ((p.id & (person(j).id == 0x3F ? 1 : 0) & 0x3F) && i != j) ok = false;
    } while (!ok);
    if (show) describeClue(i, old);
    return i;
}

void Sim::describeClue(int p, unsigned newk) {
    Person& pp = person(p);
    unsigned old = pp.known;
    pp.known = static_cast<std::uint16_t>(newk);
    if (old == newk) return;
    auto cityName = [&] { return w_.cities[static_cast<std::size_t>(pp.city & 15)].name.str(); };
    do {
        suspectDossier(p, 0, 0);
        old |= pp.known;
        const unsigned diff = old ^ pp.known;
        auto& t = g_.textBuf;
        if (diff & 0x10) {
            pp.known |= 0x10;
            t.clear();
            appendPersonName(p);
            t += " has been\nidentified as a participant\nin the plot.\n";
            notice(t, 20, 60);
        } else if (diff & 2) {
            t.clear();
            appendPersonName(p);
            t += " has been positively\nidentified as ";
            pp.known |= 2;
            appendPersonName(p);
            t += "\n";
            notice(t, 20, 60);
        } else if (diff & 1) {
            pp.known |= 1;
            t = "We have acquired a\nphotograph of ";
            appendPersonName(p);
            t += ".\n";
            notice(t, 20, 60);
        } else if (diff & 8) {
            pp.known |= 8;
            t.clear();
            appendPersonName(p);
            t += " has been\nspotted in ";
            t += cityName();
            t += "\n";
            notice(t, 20, 60);
            ++cityClueCount(pp.city & 31);
        } else if (diff & 4) {
            pp.known |= 4;
            t.clear();
            appendPersonName(p);
            t += " has been\nidentified as a member\nof the ";
            t += w_.orgs[static_cast<std::size_t>(pp.org & 15)].name.str();
            t += "\n";
            notice(t, 20, 60);
            w_.orgs[static_cast<std::size_t>(pp.org & 15)].flags |= 1;
            ++orgClueCount(pp.org & 15);
        } else if (diff & 0x20) {
            pp.known |= 0x20;
            t = "We have obtained\nadditional information\nconcerning ";
            appendPersonName(p);
            t += "\n";
            notice(t, 20, 60);
        } else if (diff & 0x100) {
            pp.known |= 0x100;
            t = "We have determined\nthe rank of\n";
            appendPersonName(p);
            t += "\n";
            notice(t, 20, 60);
        } else if (diff & 0x200) {
            pp.known |= 0x200;
            t = "We found recruiting\ninformation about\n";
            appendPersonName(p);
            t += "\n";
            notice(t, 20, 60);
        } else if (diff & 0x400) {
            pp.known |= 0x400;
            t = "We found recruiting\ninformation about\n";
            appendPersonName(p);
            t += "\n";
            notice(t, 20, 60);
            continue;
        } else {
            // A bit no branch reveals (40h, 80h, 800h+): the original loops forever.
            pp.known = static_cast<std::uint16_t>(old);
        }
    } while (pp.known != old);
    suspectDossier(p, 0, 0);
    waitKey();
}

// ---------------------------------------------------------------- new information

int Sim::describeNewInfo(int org, int city, int amount, int popup) {
    const int diff = difficulty();
    int total = 0;
    g_.popupFlag = popup;
    if (amount <= 0) return 0;
    for (int p = 0; p < w_.hdr.numPersons; ++p) {
        if (!person(p).id) continue;
        const int pOrg = person(p).org, pCity = person(p).city;
        const int dorg4 = cityDistance(static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(org & 15)].home),
                                       static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(pOrg & 15)].home));
        const int cd = cityDistance(static_cast<std::uint16_t>(w_.cities[static_cast<std::size_t>(city & 15)].code),
                                    static_cast<std::uint16_t>(w_.cities[static_cast<std::size_t>(pCity & 15)].code));
        int quot = static_cast<std::int16_t>((static_cast<long>(static_cast<std::int16_t>((dorg4 + 2) * (cd + 2))) << 8) /
                                             (amount + 1));
        const int rate = static_cast<std::int16_t>(
            (static_cast<long>(static_cast<std::int16_t>((dorg4 + 3) * (cd + 6))) << 6) / (amount + 1));
        const int w = static_cast<std::int16_t>(10000 / (diff + 2) / quot);
        {
            Person& pp = person(p);
            const int den = static_cast<std::int16_t>(pp.points / 2 + w + 1);
            pp.points = static_cast<std::int16_t>(pp.points + static_cast<long>(w) * w / den);
        }
        // (quot = 2500 / (points + 1), unused)
        for (int level = 0; level < w_.hdr.numSuspects; ++level) {
            Suspect& s = suspect(level);
            auto& st = w_.suspectState[static_cast<std::size_t>(level)];
            if (s.person == p && st.time <= w_.hdr.gameTime && st.time != -1) {
                st.progress = static_cast<std::int16_t>(st.progress + static_cast<std::int16_t>(5000 / (diff + 3)) / rate);
                int nbits = kBitcount[s.found & 0x1F];
                long guard = 0;
                while (static_cast<std::int16_t>(st.progress * (s.importance + 2)) > (nbits + 2) * (nbits + 2) * 32 &&
                       (s.found & 0x1F) != 0x1F) {
                    total += revealRandomClue(level, org, city);
                    nbits = kBitcount[s.found & 0x1F];
                    if (++guard > 1000) break;   // (no free clue slot left: the original loops)
                }
            }
        }
        if (!person(p).known && dorg4 && cd) continue;
        unsigned kn = person(p).known;
        int level = 2;
        for (int mis = 0; mis < w_.hdr.numMsgs && mis < 128; ++mis) {
            Msg& m = w_.msgs[static_cast<std::size_t>(mis)];
            if (m.sender != p) continue;
            const int t = m.msg;
            while (kBitcount[m.flags & 0xF] * 2 + level <
                       static_cast<std::int16_t>(suspect(plot(t).suspect).importance * person(p).points) / 96 &&
                   (m.flags & 0xF) != 0xF) {
                const unsigned bit8 = 1u << rnd(4);
                if (bit8 & m.flags) continue;
                suspectDossier(p, 0, 0);
                auto& tb = g_.textBuf;
                tb.clear();
                if (plot(t).flags & 0x200) tb = "       CODED MESSAGE:\n";
                if (plot(t).flags & 0x800) tb = "       MEETING NOTES:\n";
                if (!m.flags) tb += "       NEW INFORMATION\n";
                else tb += "    ADDITIONAL INFORMATION\n";
                appendPersonName(p);
                tb += " ";
                formatMessageEvent(mis);
                tb += "\n... ";
                if (bit8 & 1) {
                    tb += "on ";
                    tb += kMonths[career_.curMonth % 12];
                    tb += " ";
                    tb += itoa10(m.time / 1440 + 1);
                }
                if (plot(t).flags & 0xFFF) {
                    const int oth = m.receiver;
                    if (bit8 & 2) {
                        person(oth).known |= 2;
                        appendPersonName(oth);
                    }
                    if (bit8 & 4) {
                        tb += "of the ";
                        tb += w_.orgs[static_cast<std::size_t>(person(oth).org & 15)].name.str();
                    }
                    if (bit8 & 8) {
                        tb += "in ";
                        tb += w_.cities[static_cast<std::size_t>(m.receiverCity & 15)].name.str();
                    }
                    tb += "\n";
                    emit(SimEvent::Kind::NewInfo, {}, p);
                    if (ui_) ui_->bevelBox(16, 96, 264, 48, 3);
                    drawWrapped(32, 20, 100, 11);
                    waitKey();
                    m.flags |= static_cast<std::uint16_t>(bit8);
                    suspectDossier(p, 0, 0);
                    waitKey();
                    person(p).known |= 0x10;
                } else {
                    m.flags |= 0xE;
                    emit(SimEvent::Kind::NewInfo, {}, p);
                    waitKey();
                }
                m.flags |= static_cast<std::uint16_t>(bit8);
                if (!(person(p).roles & shl16(plot(t).suspect)) &&
                    ((diff < 1 && dorg4 + cd == 0 && (m.flags & 0xF) == 0xF) || !(plot(t).flags & 0xFFF))) {
                    person(p).roles |= shl16(plot(t).suspect);
                    kn |= 0x10;
                    person(p).known &= static_cast<std::uint16_t>(~0x10u);
                    m.flags |= 0xF;
                }
            }
            ++level;
            if ((plot(t).flags & 0xF00) && (m.flags & 0xE)) {
                const int o1 = m.receiver;
                pickClue(person(o1).org, person(o1).city, m.flags & 6, 0);
                if (m.receiverCity == person(o1).city) pickClue(person(o1).org, person(o1).city, m.flags & 8, 0);
            }
        }
        while (kn && (kBitcount[kn & 0xF] + kBitcount[(kn & 0x70F) >> 8]) * 25 < person(p).points &&
               (kn & 0x70F) != 0x70F) {
            if (org == pOrg && !(kn & 4)) {
                kn |= 4;
                break;
            }
            if (city == pCity && !(kn & 8)) {
                kn |= 8;
                break;
            }
            unsigned newbit;
            do
                newbit = 1u << rnd(12);
            while ((newbit & kn & 0x70F) || !(newbit & 0x70F));
            kn |= newbit;
        }
        if (person(p).known != kn) {
            pickClue(pOrg, pCity, kn, 1);
            ++total;
        }
    }
    if (!total && popup) {
        auto& tb = g_.textBuf;
        tb.clear();
        int p = suspect(rnd(w_.hdr.numSuspects)).person;
        g_.faceShown = -1;
        int pOrg = 0, pCity = 0;
        bool reach = false;
        switch (rnd(8 - diff)) {
        case 0:
            tb = w_.cities[static_cast<std::size_t>(person(p).city & 15)].name.str();
            ++cityClueCount(person(p).city & 31);
            break;
        case 1:
            tb = w_.orgs[static_cast<std::size_t>(person(p).org & 15)].name.str();
            ++orgClueCount(person(p).org & 15);
            w_.orgs[static_cast<std::size_t>(person(p).org & 15)].flags |= 1;
            break;
        case 2:
            tb = " ";
            g_.faceShown = person(p).id;
            break;
        case 3: {
            const std::uint16_t nb = person(p).known;
            person(p).known |= 2;
            tb.clear();
            appendPersonName(p);
            person(p).known = nb;
            break;
        }
        case 4:
        case 6:
            pOrg = w_.hdr.hereOrg;
            pCity = person(suspect(rnd(w_.hdr.numSuspects)).person).city;
            reach = true;
            break;
        case 5:
        case 7:
            pOrg = person(suspect(rnd(w_.hdr.numSuspects)).person).org;
            pCity = w_.hdr.hereCity;
            reach = true;
            break;
        }
        if (reach) {
            City& c = w_.cities[static_cast<std::size_t>(pCity & 15)];
            if (orgCanReach(w_.hdr.hereOrg, pOrg, pCity) && !(c.orgsKnown & shl16(pOrg)) && orgAlertLevel(pCity, pOrg)) {
                c.orgsKnown |= shl16(pOrg);
                setLocationKnown(pOrg, pCity, 12);
                w_.orgs[static_cast<std::size_t>(pOrg & 15)].flags |= 1;
                tb = w_.orgs[static_cast<std::size_t>(pOrg & 15)].name.str();
                appendBuildingType(pCity, pOrg);
                tb += " in ";
                tb += c.name.str();
                g_.faceShown = -2;
            } else {
                tb.clear();
            }
        }
        if (!tb.empty()) {
            emit(SimEvent::Kind::Popup, {}, g_.faceShown);
            waitKey();
        }
    }
    return total;
}

// ---------------------------------------------------------------- clues

int Sim::revealRandomClue(int n, int a, int b) {
    Suspect& s = suspect(n);
    if ((s.found & 0x1F) == 0x1F) return 0;
    unsigned bit;
    do {
        bit = 1u << rnd(5);
        if (!difficulty() && !(s.found & 0x10)) bit = 0x10;
        if (s.type == 2 && difficulty() < 2 && !(s.found & 8)) bit = 8;
    } while (s.found & bit);
    showClue(a, b, n, static_cast<int>(bit));
    return 1;
}

void Sim::deleteClue(int n) {
    for (int i = n; i < 79; ++i) {
        w_.clues[static_cast<std::size_t>(i)] = w_.clues[static_cast<std::size_t>(i + 1)];
        Clue& c = w_.clues[static_cast<std::size_t>(i)];
        if (c.suspect == -1) {
            c.org = -1;
            c.city = -1;
            break;
        }
    }
}

// show_clue(char org, char city, suspect, kind): files the clue in the first free
// slot and shows its card with the related clues (one random(12) per card).
void Sim::showClue(int org, int city, int sus, int kind) {
    if (suspect(sus).found & kind) return;
    for (int ci = 0; ci < 80; ++ci) {
        Clue& c = clue(ci);
        if (c.suspect != -1) continue;
        c.org = static_cast<std::int8_t>(org);
        c.city = static_cast<std::int8_t>(city);
        c.suspect = static_cast<std::int16_t>(sus);
        if (kind > 31) kind >>= 5;
        c.kind = static_cast<std::int16_t>(encodeClue(ci, kind));
        c.text = static_cast<std::int16_t>(loadClueText(sus, 0));
        suspect(sus).found |= static_cast<std::uint16_t>(kind);
        if (ui_) ui_->pageFrame("Clue", 9);
        auto& tb = g_.textBuf;
        tb = "\x87Source: ";
        tb += w_.orgs[static_cast<std::size_t>(c.org & 15)].name.str();
        tb += "/";
        tb += w_.cities[static_cast<std::size_t>(c.city & 15)].name.str();
        tb += "\n";
        clueTextWrapper(ci);
        if (c.org > 1) g_.clueIcon = g_.popupFlag == 1 ? 1 : 0;
        tb += "\n\x87Method: ";
        tb += (g_.clueIcon >= 0 && g_.clueIcon < 7) ? kClueMethodNames[g_.clueIcon] : "";
        emit(SimEvent::Kind::Clue, {}, ci);
        drawWrapped(31, 16, 17, 5);
        if (ui_) {
            if (kind == 1) {
                ui_->portrait(g_.faceShown, 264, 18);
                ui_->sprite(108, 299, 46);
            } else {
                ui_->sprite(g_.clueIcon + 96, 272, 16);
            }
            ui_->text("\x8CRelated Clues:", 16, 66, 5);
        }
        int line = 76, picY = 73;
        bool shown = false;
        for (int phase = 0; phase < 2; ++phase) {
            for (int cj = 0; cj < 80; ++cj) {
                const Clue& o = clue(cj);
                if (o.suspect == -1 || ci == cj) continue;
                if (line > 180) break;
                if (!((phase && c.kind == o.kind) || (!phase && c.text == o.text))) continue;
                tb.clear();
                const int wid = rnd(12) + 4;
                if (ui_) ui_->cardPanel(wid, line - 2);
                if ((o.kind >> 8) == 16) clueTextWrapper(cj);
                else describePerson(cj);
                tb += "\n";
                shown = true;
                emit(SimEvent::Kind::Clue, "related", cj);
                line = drawWrapped(32, wid + 8, line, 0);
                line += 4;
                if ((o.kind >> 8) == 1) {
                    if (ui_) {
                        ui_->portrait(person(suspect(o.suspect).person).id, 264, clampTo(picY, 0, 150));
                        ui_->sprite(108, 299, clampTo(picY + 24, 0, 176));
                    }
                    picY += 40;
                }
            }
        }
        if (!shown && ui_) ui_->text("...none", 16, 72, 5);
        waitKey();
        g_.faceShown = -1;
        return;
    }
}

int Sim::encodeClue(int ci, int kind) {
    const int who = suspect(clue(ci).suspect).person;
    switch (kind) {
    case 8:
        ++cityClueCount(person(who).city & 31);
        return (kind << 8) + person(who).city;
    case 4:
        ++orgClueCount(person(who).org & 15);
        w_.orgs[static_cast<std::size_t>(person(who).org & 15)].flags |= 1;
        g_.textBuf += w_.orgs[static_cast<std::size_t>(person(who).org & 15)].name.str();
        return (kind << 8) + person(who).org;
    case 1:
    case 2:
        return (kind << 8) + who;
    case 16:
        return clue(ci).suspect + (kind << 8);
    }
    return 0;   // (not reached: reveal_random_clue picks one of the five)
}

int Sim::loadClueText(int sus, int full) {
    int val = suspect(sus).info;
    if (full) val &= 0x7F;
    else val >>= 1;
    if (sus < activeSuspects()) return (static_cast<std::uint8_t>(suspect(sus).type) << 8) + val;
    int pos = person(suspect(sus).person).id;
    std::string key = "*C0000";
    key[5] = static_cast<char>(key[5] + pos % 6);
    key[3] = static_cast<char>(key[3] + (pos >> 3) % 4);
    const std::size_t at = g_.textBuf.size();
    lookupText("clues.txt", key, 5, true);
    // text_buf[pos] = 0, then the byte after it: the text's second character
    const char second = g_.textBuf.size() > at + 1 ? g_.textBuf[at + 1] : 0;
    g_.textBuf.resize(at);
    return ((second & 15) << 8) + val;
}

void Sim::describePerson(int ci) {
    const Clue& c = clue(ci);
    if (c.text != -1) {
        appendNumber(c.text);
        g_.textBuf += kClueKindNames[clueKindIndex(c.kind, c.text)];
    }
    if (c.kind != -1) {
        describePersonId(ci, c.kind);
        g_.textBuf += ".";
    }
}

int Sim::clueKindIndex(int kind, int text) {
    int idx = ((text >> 8) & 7) * 5;
    for (int k = static_cast<std::int16_t>(kind) >> 8; k > 1; k >>= 1) ++idx;
    return idx < 40 ? idx : 39;
}

void Sim::describePersonId(int ci, int id) {
    const int n = id & 0xFF;
    switch (static_cast<std::int16_t>(id) >> 8) {
    case 8:
        g_.textBuf += w_.cities[static_cast<std::size_t>(n & 15)].name.str();
        break;
    case 4:
        g_.textBuf += w_.orgs[static_cast<std::size_t>(n & 15)].name.str();
        w_.orgs[static_cast<std::size_t>(n & 15)].flags |= 1;
        break;
    case 2:
        personName(n);
        break;
    case 1:
        g_.textBuf += "this face.";
        g_.faceShown = person(n).id;
        break;
    case 16:
        g_.textBuf += clue(ci).suspect < activeSuspects() ? "Valkerie" : "Thunderbolt";
        break;
    }
}

void Sim::appendNumber(int num) {
    if (num == -1) return;
    const int kind = (num >> 8) & 15;
    if (kind < 8) g_.textBuf += kNumberPrefix[kind][(num >> 4) & 7];
    g_.textBuf += itoa10((num & 0x3F) + 1);
    if (kind == 5) g_.textBuf += "00";
    if (kind == 6) g_.textBuf += "000";
}

void Sim::clueText(int kind, int text, int ci) {
    kind = static_cast<std::int16_t>(kind);
    text = static_cast<std::int16_t>(text);
    const int num = kind & 0xFF;
    std::string key = "*C00";
    key[2] = static_cast<char>(key[2] + ((text >> 8) & 15));
    for (int pos = kind >> 8; pos > 1; pos >>= 1) ++key[3];
    auto& tb = g_.textBuf;
    std::size_t pos = tb.size();
    appendNumber(text);
    g_.subst[0] = tb.substr(pos);
    tb.resize(pos);
    std::size_t cut = 1;
    switch (kind >> 8) {
    case 1:
        g_.faceShown = person(num).id;
        break;
    case 2:
        personName(num);
        g_.subst[1] = tb.substr(pos);
        tb.resize(pos);
        break;
    case 4:
        g_.subst[2] = w_.orgs[static_cast<std::size_t>(num & 15)].name.str();
        break;
    case 8:
        g_.subst[3] = w_.cities[static_cast<std::size_t>(num & 15)].name.str();
        break;
    case 16:
        cut = 2;
        if (clue(ci).suspect < activeSuspects()) {
            key = "*C0000";
            key[5] = static_cast<char>(key[5] + num);
            key[3] = static_cast<char>(key[3] + w_.hdr.msgDay % 10);
            key[2] = static_cast<char>(key[2] + w_.hdr.msgDay / 10);
        } else {
            const int id = person(suspect(clue(ci).suspect).person).id;
            key = "*C0000";
            key[5] = static_cast<char>(key[5] + id % 6);
            key[3] = static_cast<char>(key[3] + (id >> 3) % 4);
        }
        break;
    }
    pos = tb.size();
    lookupText("clues.txt", key, 5, true);
    g_.clueIcon = (pos < tb.size() ? tb[pos] : 0) & 15;
    if (pos + cut <= tb.size()) tb.erase(pos, cut);
    else tb.resize(pos);
}

// suspect_dossier(who, x, y): the dossier page (y = 0) or the panel embedded in
// another screen (y != 0). The state it changes: the organization known, and the
// message events (format_message_event) of his messages; the layout (y) decides
// when a page is full and the message is shown again on a new page.
void Sim::suspectDossier(int who, int x, int y) {
    emit(SimEvent::Kind::Dossier, {}, who);
    auto& tb = g_.textBuf;
    auto text = [&](int tx, int ty, int c) {
        if (ui_) ui_->text(tb, tx, ty, c);
    };
    auto line = [&](int x1, int y1, int x2, int y2, int c) {
        if (ui_) ui_->line(x1, y1, x2, y2, c);
    };
    const int embedded = y;
    if (y) {
        x += 4;
    } else {
        tb.clear();
        appendPersonName(who);
        if (ui_) ui_->pageFrame(tb, 3);
        y = 18;
        x += 10;
    }
    y += 2;
    tb = "Name:\x80";
    appendPersonName(who);
    text(x, y, 7);
    Person& p = person(who);
    if (p.known & 0x100) {
        int nearCity = (p.id >> 2) & 7;
        for (int ei = 0; ei < w_.hdr.numSuspects; ++ei)
            if (suspect(ei).person == who) nearCity = suspect(ei).rank;
        tb = "Rank:\x80";
        tb += (nearCity >= 0 && nearCity < 8) ? kRankNames[nearCity] : "";
        y += 8;
        text(x, y, 7);
    }
    if (p.known & 4) {
        w_.orgs[static_cast<std::size_t>(p.org & 15)].flags |= 1;
        tb = "Org: \x80";
        tb += w_.orgs[static_cast<std::size_t>(p.org & 15)].name.str();
        y += 8;
        text(x, y, 7);
    }
    if (p.known & 8) {
        tb = "City: \x80";
        tb += w_.cities[static_cast<std::size_t>(p.city & 15)].name.str();
        y += 8;
        text(x, y, 7);
    }
    tb = "Recruited";
    if (p.known & 0x400) {
        for (int ei = 0; ei < w_.hdr.numSuspects; ++ei) {
            if (p.org == person(suspect(ei).person).org) {
                if (who != suspect(ei).person) {
                    tb += " by ";
                    personName(suspect(ei).person);
                }
                break;
            }
        }
    }
    if (p.known & 0x200) {
        int nearCity = -1, dist = 999, ei = 0;
        do {
            if (ei != p.city && cityMapDistance(ei, p.city) < dist) {
                nearCity = ei;
                dist = cityMapDistance(ei, p.city);
            }
        } while (++ei < 16);
        if (nearCity != -1) {
            tb += " in ";
            tb += w_.cities[static_cast<std::size_t>(nearCity)].name.str();
        }
    }
    if (tb.size() > 12 && !embedded) {
        tb += ".";
        y += 8;
        text(x, y, 7);
    }
    if ((p.known & 1) && !embedded && ui_) {
        ui_->portrait(p.id, 264, 8);
        ui_->sprite(108, 299, 40);
    }
    bool role = false;
    y += 10;
    line(x, y - 2, x + 128, y - 2, 12);
    for (int ei = 0; ei < w_.hdr.numSuspects; ++ei) {
        if (p.roles & shl16(ei)) {
            tb = "Role: \x80";
            tb += suspect(ei).name.str();
            text(x, y, 7);
            role = true;
        }
    }
    if (!role) {
        if ((p.known & 0x1F) == 0x1F && !(p.flags & 0x7FFF)) tb = "Role: \x80NOT INVOLVED";
        else tb = "Role: \x80unknown";
        text(x, y, 7);
    }
    if (p.fate == -1) {
        tb = "(Under Arrest)";
        y += 8;
        text(x, y, 5);
    }
    if (p.fate == -2) {
        tb = "(In Hiding)";
        y += 8;
        text(x, y, 5);
    }
    if (p.fate == -3) {
        tb = "(Turned)";
        y += 8;
        text(x, y, 5);
    }
    line(x, y + 8, x + 128, y + 8, 12);
    y += 2;
    y = clampTo(y + 8, (p.known & 1) ? embedded + 54 : 0, 99);
    for (int ei = 0; ei < w_.hdr.numMsgs && ei < 128; ++ei) {
        Msg& m = w_.msgs[static_cast<std::size_t>(ei)];
        if (!(m.sender == who && m.flags && !embedded)) continue;
        for (;;) {
            tb.clear();
            formatMessageEvent(ei);
            y = drawWrapped(37, x, y, 5);
            const int msg = m.msg;
            if (m.flags & 0x8000) {
                tb = "'";
                loadMessageText(msg);
                tb += "'";
                y = drawWrapped(36, x + 4, y, 12);
            } else {
                const int k = plot(msg).flags & 0xF00;
                if (k == 0x200 || k == 0x300) {
                    if (ui_) ui_->textOpaque("(message not decoded)", x + 4, y, 4);
                    y += 8;
                }
                if (k == 0x800 || k == 0x900) {
                    tb.clear();
                    loadMessageText(msg);
                    garbleMessage(m.flags, 5);
                    y = drawWrapped(36, x + 4, y, 12);
                }
            }
            if (y < 192) break;
            if (ui_) ui_->textOpaque("...more", 256, 192, 12);
            waitKey();
            tb.clear();
            appendPersonName(who);
            if (ui_) ui_->pageFrame(tb, 3);
            y = 16;
        }
    }
}

void Sim::formatMessageEvent(int e) {
    Msg& me = w_.msgs[static_cast<std::size_t>(e & 127)];
    const int m = me.msg;
    auto& tb = g_.textBuf;
    tb += m >= 0 ? plot(m).name.str() : (m & 1) ? "Rcvd msg fm" : "Sent msg to";
    if ((plot(m).flags & 0xFFF) || m < 0) {
        const int who = me.sender, who2 = me.receiver;
        int other = -1;
        if (msgTime(e + 1) == me.time) other = e + 1;
        if (msgTime(e - 1) == me.time) other = e - 1;
        if (me.flags & 2) {
            tb += " ";
            appendPersonName(who2);
        } else {
            tb += " someone";
        }
        if (me.flags & 4) {
            tb += " of the ";
            tb += w_.orgs[static_cast<std::size_t>(person(who2).org & 15)].abbrev.str();
        }
        if (me.flags & 8) {
            tb += " in ";
            tb += w_.cities[static_cast<std::size_t>(me.receiverCity & 15)].name.str();
        }
        Person& p2 = person(who2);
        Person& p1 = person(who);
        if (((me.flags & 2) && (p2.known & 2)) ||
            ((me.flags & 4) && (me.flags & 8) && (p2.known & 4) && (p2.known & 8))) {
            orMsgFlags(e, p2.known & 6);
            orMsgFlags(other, p1.known & 6);
            p2.known |= static_cast<std::uint16_t>(msgFlags(e) & 6);
            if (me.receiverCity == p2.city) {
                orMsgFlags(e, p2.known & 8);
                p2.known |= static_cast<std::uint16_t>(msgFlags(e) & 8);
            }
        }
        const unsigned of = msgFlags(other);
        if (((of & 2) && (p1.known & 2)) || ((of & 4) && (of & 8) && (p1.known & 4) && (p1.known & 8))) {
            orMsgFlags(other, p1.known & 6);
            orMsgFlags(e, p2.known & 6);
            p1.known |= static_cast<std::uint16_t>(msgFlags(other) & 6);
            if (msgReceiverCity(other) == p1.city) {
                orMsgFlags(other, p1.known & 8);
                p1.known |= static_cast<std::uint16_t>(msgFlags(other) & 8);
            }
        }
    }
    if (me.flags & 1) {
        tb += " on ";
        tb += kMonths[career_.curMonth % 12];
        tb += " ";
        tb += itoa10(me.time / 1440 + 1);
    }
    tb += ".";
}

}  // namespace redux
