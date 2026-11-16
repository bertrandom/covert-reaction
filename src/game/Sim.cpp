// TIME.C, CHRONO.C and the out-of-table accessors. See Sim.h.
#include "game/Sim.h"

#include <stdexcept>

#include "formats/Font.h"
#include "formats/TextDb.h"

namespace redux {

namespace {

// 1 << n as MSC's "shl ax, cl" on a 386+: the count is CL & 31, and a 16-bit
// register shifted by 16..31 is 0.
std::uint16_t shl16(int n) {
    n &= 31;
    return n < 16 ? static_cast<std::uint16_t>(1u << n) : 0;
}

}  // namespace

Sim::Sim(World& world, Rng& rng, const GameData& data, Career& career)
    : w_(world), rng_(rng), data_(data), career_(career) {
    // persons[-1] in GAME.EXE: DGROUP 0084h, the bytes "ment please...\n\0" of the
    // literal "One moment please...\n" (GAME.C main) just before persons[] at 0094h.
    static const char kM1[16] = {'m', 'e', 'n', 't', ' ', 'p', 'l', 'e', 'a', 's', 'e', '.', '.', '.', '\n', 0};
    auto word = [](int i) {
        return static_cast<std::uint16_t>(static_cast<std::uint8_t>(kM1[i]) |
                                          static_cast<std::uint8_t>(kM1[i + 1]) << 8);
    };
    personM1_.org = static_cast<std::int16_t>(word(0));
    personM1_.id = static_cast<std::int16_t>(word(2));
    personM1_.city = static_cast<std::int16_t>(word(4));
    personM1_.known = word(6);
    personM1_.flags = word(8);
    personM1_.roles = word(10);
    personM1_.points = static_cast<std::int16_t>(word(12));
    personM1_.fate = static_cast<std::int16_t>(word(14));
    fonts_ = std::make_unique<FontSet>(FontSet::load(data_));
}

Sim::~Sim() = default;

// ---------------------------------------------------------------- accessors

Person& Sim::person(int i) {
    if (i >= 0 && i < 64) return w_.persons[static_cast<std::size_t>(i)];
    // persons[-1] is a string literal (see the constructor); further out (a stale
    // byte as an index) the port reads it too. Writes land there and are kept, as
    // the original's would be in its data.
    return personM1_;
}

// suspects[] holds num_suspects entries from env.sve; GAME's table has room for
// 43 and the rest is zero (person 0, the MasterMind's slot). Indices past the
// table (a plot's receiving-slot byte) are given the same zero entry.
Suspect& Sim::suspect(int i) {
    if (i >= 0 && i < static_cast<int>(w_.suspects.size())) return w_.suspects[static_cast<std::size_t>(i)];
    suspectOut_ = Suspect{};
    suspectOut_.person = (i < 0) ? 0x7FFF : 0;   // suspects[-1]: face picture handles, no person
    return suspectOut_;
}

// plots[] past num_plots are zero in GAME's 64-entry table.
Plot& Sim::plot(int i) {
    if (i >= 0 && i < static_cast<int>(w_.plots.size())) return w_.plots[static_cast<std::size_t>(i)];
    plotOut_ = Plot{};
    plotOut_.suspect = 0;
    plotOut_.state = 0;
    return plotOut_;
}

// format_message_event reads msgs[e - 1] and msgs[e + 1]; for e = 0 that is
// msgs[-1], which in GAME's DGROUP (msgs at 7CC2h) overlaps the end of cities[15]
// (7C9Ch): .time is cities[15].size, .flags cities[15].orgs_visited, and
// .receiver_city the high byte of num_persons (7CC0h). msgs[128] (83C2h) is
// face_shown, popup_flag and plots[0]: .time = popup_flag, .flags = plots[0].state,
// .receiver_city = plots[0].name[3].
std::uint16_t Sim::msgFlags(int i) const {
    if (i >= 0 && i < 128) return w_.msgs[static_cast<std::size_t>(i)].flags;
    if (i == -1) return w_.cities[15].orgsVisited;
    if (i == 128) return w_.plots.empty() ? 0 : static_cast<std::uint16_t>(w_.plots[0].state);
    return 0;
}

void Sim::orMsgFlags(int i, unsigned v) {
    if (i >= 0 && i < 128) w_.msgs[static_cast<std::size_t>(i)].flags |= static_cast<std::uint16_t>(v);
    else if (i == -1) w_.cities[15].orgsVisited |= static_cast<std::uint16_t>(v);
    else if (i == 128 && !w_.plots.empty())
        w_.plots[0].state = static_cast<std::int16_t>(static_cast<std::uint16_t>(w_.plots[0].state) | v);
}

std::int16_t Sim::msgTime(int i) const {
    if (i >= 0 && i < 128) return w_.msgs[static_cast<std::size_t>(i)].time;
    if (i == -1) return w_.cities[15].size;
    if (i == 128) return static_cast<std::int16_t>(g_.popupFlag);
    return 0;
}

int Sim::msgReceiverCity(int i) const {
    if (i >= 0 && i < 128) return w_.msgs[static_cast<std::size_t>(i)].receiverCity;
    if (i == -1) return static_cast<std::int8_t>(static_cast<std::uint16_t>(w_.hdr.numPersons) >> 8);
    if (i == 128 && !w_.plots.empty()) return static_cast<std::int8_t>(w_.plots[0].name.raw[3]);
    return 0;
}

const TextDb& Sim::textDb(const char* file) {
    std::unique_ptr<TextDb>* slot = &text_;
    std::string f = file;
    if (f == "clues.txt") slot = &clues_;
    else if (f == "prose.dta") slot = &prose_;
    if (!*slot) *slot = std::make_unique<TextDb>(TextDb::load(data_, f));
    return **slot;
}

int Sim::post(SimEvent e) {
    // With a UI the events go to it as they happen (it may wait there); without
    // one they are listed for takeEvents().
    if (ui_) return ui_->event(e);
    events_.push_back(std::move(e));
    return -1;
}

int Sim::emit(SimEvent::Kind k, std::string title, int index) {
    return post(SimEvent{k, std::move(title), g_.textBuf, index});
}

int Sim::emitText(SimEvent::Kind k, std::string title, std::string text, int index) {
    return post(SimEvent{k, std::move(title), std::move(text), index});
}

int Sim::notice(const std::string& s, int x, int y, unsigned flags) {
    SimEvent e{SimEvent::Kind::Notice, {}, s, -1};
    e.x = x;
    e.y = y;
    e.flags = flags;
    return post(std::move(e));
}

int Sim::drawWrapped(int width, int x, int y, int color) {
    if (ui_) ui_->wrapped(g_.textBuf, width, x, y, color);
    return drawWrappedText(width, y);
}

// ---------------------------------------------------------------- TIME.C

void Sim::advanceTime(int minutes) {
    if (minutes == 0) drawStatusBar();
    while (minutes >= 60 && !caseOver_) {
        processEvents(60, false);
        minutes -= 60;
        if (w_.hdr.numMsgs > 5 && ui_ && !caseOver_) ui_->retrace(30);
    }
    if (minutes != 0 && !caseOver_) processEvents(30, false);
}

std::string Sim::formatDateTime(unsigned t) {
    static const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    t &= 0xFFFF;
    std::string s = "00:00 AM Jun 00";
    g_.clockMinute = static_cast<int>(t % 60);
    s[3] = static_cast<char>(g_.clockMinute / 10 + '0');
    s[4] = static_cast<char>(g_.clockMinute % 10 + '0');
    t /= 60;
    g_.clockHour = static_cast<int>(t % 24);
    int h = (g_.clockHour + 11) % 12 + 1;
    s[0] = static_cast<char>(h / 10 + '0');
    s[1] = static_cast<char>(h % 10 + '0');
    s[6] = g_.clockHour > 11 ? 'P' : 'A';
    s = s.substr(0, 9) + kMonths[career_.curMonth % 12] + " 10";
    g_.clockDay = static_cast<int>(t / 24 + 1);
    s[13] = static_cast<char>(g_.clockDay / 10 + '0');
    s[14] = static_cast<char>(g_.clockDay % 10 + '0');
    return s;
}

void Sim::endCase() {
    // mm_known = persons[0].known; save_env("env.sve"); require_disk(1);
    // set_exit_state(); ENV[0] = 9; exit()
    career_.mm.known = w_.persons[0].known;
    setExitState();
    caseOver_ = true;
    emit(SimEvent::Kind::CaseOver);
}

int Sim::setExitState() {
    // ENV+48h = describe_alert_level(here_org, here_city); ENV+50h = difficulty
    // (describe_alert_level appends its word to text_buf, which isn't shown)
    int lvl = describeAlertLevel(w_.hdr.hereOrg, w_.hdr.hereCity);
    w_.hdr.var48 = static_cast<std::int16_t>(lvl);
    w_.hdr.var50 = static_cast<std::int16_t>(difficulty());
    return lvl;
}

void Sim::processEvents(int minutes, bool force) {
    if (caseOver_) return;
    auto& h = w_.hdr;
    const int diff = difficulty();
    int ohourf = g_.clockHour;
    int odaye = g_.clockDay;
    setClockBase(clockBase() + static_cast<unsigned>(minutes));
    drawStatusBar();
    if (ui_) ui_->retrace(2);
    if (!((ohourf < 12 && g_.clockHour >= 12) || odaye != g_.clockDay || force)) return;
    h.gameTime = static_cast<std::int16_t>(h.gameTime + 1);
    for (auto& c : w_.cities) c.orgsVisited = 0;
    for (int pa = 0; pa < h.numPersons; ++pa) {
        Person& p = person(pa);
        if (p.id != 0 && p.fate >= 0) p.fate = static_cast<std::int16_t>(p.fate - p.fate / (diff + 3));
    }
    heat() = static_cast<std::int16_t>(heat() - heat() / (diff + 3));
    int nfa = 0;
    g_.hidingTarget = -2;
    for (int il = 0; il < h.numPlots - 1; ++il)
        if (checkOutOfHiding(il) != -1) ++nfa;
    if (rnd(10) >= diff + 4 && h.numMsgs > 5 && nfa != 0 && !force) return;
    if (nfa <= diff) {
        g_.hidingTarget = -1;
        for (int il = 0; il < h.numPlots - 1; ++il) {
            if (checkOutOfHiding(il) != -1) {
                nfa = 1;
                break;
            }
        }
    }
    int hida = 0;
    int triesi = 0, pa = 0;
    for (;;) {
        triesi = 0;
        do {
            int rsl = rnd(activeSuspects());
            pa = suspect(rsl).person;
            if (person(pa).fate >= 0 && pa != g_.hidingTarget) break;
            ++triesi;
        } while (triesi < 999);
        if (triesi != 999 && h.numMsgs > 8 &&
            (person(pa).city != h.hereCity || person(pa).org != h.hereOrg || g_.inBuilding)) {
            int freee = 1;
            for (int il = 0; il < h.numPlots; ++il) {
                if (suspect(plot(il).suspect).person == pa && plot(il).state == 999) {
                    freee = 0;
                    break;
                }
            }
            if ((freee || (nfa == 0 && g_.hidingTarget < 0)) && (!hida || !(person(pa).known & 0xF))) {
                person(pa).fate = -2;
                if (person(pa).known != 0) {
                    g_.textBuf = "Bulletin: ";
                    appendPersonName(pa);
                    g_.textBuf += " has gone into hiding.\n";
                    teletype("Surveillance Team");
                }
                addChronologyEntry(8, pa);
                hida = 1;
                continue;
            }
        }
        break;
    }
    if ((triesi == 999 && nfa == 0) || clockBase() > 40000) {
        endCase();
        return;
    }
    if (force) return;
    if (nfa) {
        int pij = 0;
        long guard = 0;
        do {
            pij = rnd(h.numPlots - 1);
            nfa = checkOutOfHiding(pij);
            if (++guard > 10000000) throw std::runtime_error("process_events: no due plot step (the original hangs)");
        } while (nfa == -1);
        Plot& pp = plot(pij);
        if (!(pp.flags & 0xFFF)) {
            pp.state = h.gameTime;
            auto& st = w_.suspectState[static_cast<std::size_t>(pp.suspect)];
            if (st.time == -1) st.time = h.gameTime;
        } else {
            Plot& pn = plot(nfa);
            if ((pp.flags & 0x1000) && person(suspect(pn.suspect).person).fate < 0) {
                pp.state = 0;
            } else if (pn.state != 999) {
                pp.state = pn.state;
            } else {
                pp.state = h.gameTime;
                auto& st = w_.suspectState[static_cast<std::size_t>(pp.suspect)];
                if (st.time == -1) st.time = h.gameTime;
                pn.state = h.gameTime;
                auto& st2 = w_.suspectState[static_cast<std::size_t>(pn.suspect)];
                if (st2.time == -1) st2.time = h.gameTime;
                for (int il = 0; il < h.numPlots; ++il)
                    if (pp.msg == plot(il).msg && il != pij && il != nfa) plot(il).state = -1;
                if (suspect(pp.suspect).person == suspect(pn.suspect).person) {
                    pp.state = -1;
                    pn.state = -1;
                }
            }
        }
    }
    for (int il = 0; il < h.numPlots; ++il) {
        if (plot(il).state != h.gameTime) continue;
        addChronologyEntry(0, il);
        Plot& pl = plot(il);
        pa = suspect(pl.suspect).person;
        for (int kk = 0; kk < 4; ++kk) {
            Item& it = w_.items[static_cast<std::size_t>(kk)];
            if (pl.items & (1 << kk)) {
                it.holder = static_cast<std::int8_t>(pa);
                if (rnd(2) >= diff && !(pl.flags & 0x2000) && h.numMsgs > 5) {
                    g_.textBuf = "INTERPOL Bulletin:\n";
                    g_.textBuf += it.name.str();
                    g_.textBuf += " spotted in ";
                    g_.textBuf += w_.cities[static_cast<std::size_t>(person(pa).city & 15)].name.str();
                    g_.textBuf += "!\n";
                    teletype("INTERPOL");
                    addChronologyEntry(11, (kk << 8) + person(pa).city);
                }
            }
            if (pl.items & (0x100 << kk)) it.holder = -1;
        }
        if (pl.flags & 0x2000) {
            g_.textBuf.clear();
            loadMessageText(il);
            teletype("");
            continue;
        }
        if ((pl.flags & 0xF00) == 0x200 && h.numMsgs > 5) {
            g_.textBuf = "Message traffic detected.\n... ";
            if (rnd(2)) {
                g_.textBuf += "FROM: ";
                int c = person(pa).city;
                g_.textBuf += w_.cities[static_cast<std::size_t>(c & 15)].name.str();
                ++cityClueCount(c & 31);
            } else {
                g_.textBuf += "TO: ";
                int c = person(suspect(pl.flags & 0xFF).person).city;
                g_.textBuf += w_.cities[static_cast<std::size_t>(c & 15)].name.str();
                ++cityClueCount(c & 31);
            }
            g_.textBuf += " ...\n";
            teletype("Satellite Intercept");
        }
        int sidxb = setLocationKnown(person(pa).org, person(pa).city, 0);
        if (person(pa).fate == -3 ||
            (sidxb != -1 && rnd(50) < w_.sites[static_cast<std::size_t>(sidxb)].strength)) {
            for (int kk = 0; kk < h.numMsgs && kk < 128; ++kk) {
                Msg& m = w_.msgs[static_cast<std::size_t>(kk)];
                if (!(m.type == 0 && m.msg == il && static_cast<std::uint16_t>(m.time) == clockBase())) continue;
                m.flags |= 0x800F;
                bool turned = person(pa).fate == -3;
                if (turned) {
                    g_.textBuf = "Double Agent: ";
                    person(m.receiver).known |= 0xC;
                    person(m.receiver).roles |= shl16(pl.flags);
                } else {
                    g_.textBuf = "Wiretap at ";
                }
                g_.textBuf += w_.orgs[static_cast<std::size_t>(person(pa).org & 15)].name.str();
                g_.textBuf += "/";
                g_.textBuf += w_.cities[static_cast<std::size_t>(person(pa).city & 15)].name.str();
                g_.textBuf += " reveals ";
                if (person(pa).fate == -3) {
                    g_.textBuf += (person(pa).id & 1) ? "he " : "she ";
                } else {
                    appendPersonName(pa);
                    g_.textBuf += " ";
                }
                formatMessageEvent(kk);
                g_.textBuf += "\n";
                if ((pl.flags & 0xF00) == 0x800) {
                    loadMessageText(il);
                    person(m.receiver).roles |= person(m.receiver).flags;
                } else if (person(pa).fate == -3) {
                    loadMessageText(il);
                }
                teletype("");
            }
        }
        if ((pl.flags & 0xF00) == 0x900 && (person(pa).known & 1)) {
            for (int kk = 0; kk < h.numMsgs && kk < 128; ++kk) {
                Msg& m = w_.msgs[static_cast<std::size_t>(kk)];
                if (m.type == 0 && m.msg == il && m.senderCity != m.receiverCity) {
                    g_.textBuf = "From: ";
                    g_.textBuf += w_.orgs[1].name.str();
                    g_.textBuf += "/";
                    g_.textBuf += w_.cities[static_cast<std::size_t>(m.senderCity & 15)].name.str();
                    g_.textBuf += "\n";
                    appendPersonName(pa);
                    g_.textBuf += " has just returned from a trip to ";
                    g_.textBuf += w_.cities[static_cast<std::size_t>(m.receiverCity & 15)].name.str();
                    g_.textBuf += ".\n";
                    teletype("Airport Surveillance");
                    m.flags |= 9;
                }
            }
        }
    }
    int movedi = 0;
    for (int il = 0; il < h.numSuspects; ++il) {
        if (movedi || !(suspect(il).flags & 8)) continue;
        pa = suspect(il).person;
        if (!(person(pa).known & 8)) continue;
        if (!(rnd(128 / (diff + 1)) < person(pa).fate && person(pa).city != h.hereCity)) continue;
        for (int td = 0; td < 4; ++td) {
            int ncityi = rnd(16);
            if (!orgAlertLevel(ncityi, person(pa).org)) continue;
            int locd = pickClue(person(pa).org, ncityi, 0, 0);
            if (person(locd).flags == 0 && person(locd).known == 0) {
                g_.textBuf.clear();
                appendPersonName(pa);
                g_.textBuf += " seems to have departed from ";
                g_.textBuf += w_.cities[static_cast<std::size_t>(person(pa).city & 15)].name.str();
                g_.textBuf += ".\n";
                teletype("Airport Surveillance");
                addChronologyEntry(1, (pa << 8) + person(pa).city);
                freeLocationIfUnused(locd);
                person(pa).city = static_cast<std::int16_t>(ncityi);
                removeLocationType(pa, 8);
                person(pa).fate = static_cast<std::int16_t>(person(pa).fate / 2);
                movedi = 1;
                break;
            }
        }
    }
}

void Sim::removeLocationType(int p, unsigned bits) {
    Person& pp = person(p);
    pp.known = static_cast<std::uint16_t>(pp.known & ~bits);
    pp.points = static_cast<std::int16_t>(pp.points / 2);
    if (bits == 1) pp.known |= 0x80;
    for (int i = 0; i < w_.hdr.numPlots; ++i) {
        int sg = plot(i).suspect;
        if (suspect(sg).person == p) {
            suspect(sg).found = static_cast<std::uint16_t>(suspect(sg).found & ~bits);
            if (sg >= 0 && sg < static_cast<int>(w_.suspectState.size())) {
                auto& st = w_.suspectState[static_cast<std::size_t>(sg)];
                st.progress = static_cast<std::int16_t>(st.progress / 2);
            }
        }
    }
    for (auto& c : w_.clues) {
        if (suspect(c.suspect).person == p && (c.kind >> 8) == static_cast<int>(bits)) c.suspect = -1;
    }
}

int Sim::checkOutOfHiding(int n) {
    const auto& h = w_.hdr;
    if (plot(n).state != 999) return -1;
    int a = suspect(plot(n).suspect).person;
    int sto = person(a).fate;
    if (sto == -3 && plot(n).severity != 0) return -1;
    for (int il = 0; il < n; ++il)
        if (plot(il).state == 999 && plot(n).suspect == plot(il).suspect) return -1;
    if (!(plot(n).flags & 0xFFF)) {
        if (h.numMsgs <= 5) return -1;
        sto = person(a).fate;
        if (sto < 0 && sto != -3) {
            checkSpecialLocation(a);
            return -1;
        }
        return 1;
    }
    int bin = plot(n).flags & 0xFF;
    int b = suspect(bin).person;
    bool flagj = person(b).fate < 0 && person(b).fate != -3 && (plot(n).flags & 0x1000);
    int resm = -1;
    for (int il = 0; il < h.numPlots; ++il) {
        if (plot(il).suspect == bin) {   // (char) suspect, sign-extended, against the slot byte
            if (plot(n).msg == plot(il).msg) {
                resm = il;
                break;
            }
            if (plot(il).state == 999 && !flagj) return -1;
        }
    }
    if (person(a).fate == -1) {
        checkSpecialLocation(a);
        return -1;
    }
    if (person(b).fate == -1) {
        checkSpecialLocation(b);
        return -1;
    }
    if (person(a).fate == -2 && person(b).fate != -1) {
        person(a).fate = 0;
        g_.textBuf = "Bulletin:\n";
        appendPersonName(a);
        g_.textBuf += " has come out of hiding!\n";
        teletype("Surveillance Team");
        addChronologyEntry(9, a);
    }
    if (person(b).fate == -2 && person(a).fate != -1) {
        person(b).fate = 0;
        g_.textBuf = "Bulletin:\n";
        appendPersonName(b);
        g_.textBuf += " has come out of hiding!\n";
        teletype("Surveillance Team");
        addChronologyEntry(9, b);
    }
    return resm;
}

int Sim::checkSpecialLocation(int p) {
    const int diff = difficulty();
    for (int i = 0; i < activeSuspects(); ++i)
        if (suspect(i).flags & 0x100) g_.mastermind = suspect(i).person;
    if ((g_.hidingTarget == -1 || rnd(8) < diff) && person(p).fate == -1 && diff > 1 &&
        !(person(p).known & 0x40) && person(g_.mastermind).fate != -1) {
        g_.hidingTarget = p;
        w_.hdr.jailbreakPerson = static_cast<std::int16_t>(p);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- CHRONO.C

void Sim::addChronologyEntry(int kind, int msg) {
    auto& h = w_.hdr;
    if (h.numMsgs >= 0 && h.numMsgs < 128) {
        Msg& m = w_.msgs[static_cast<std::size_t>(h.numMsgs)];
        m.time = static_cast<std::int16_t>(clockBase());
        m.type = static_cast<std::int16_t>(kind);
        m.msg = static_cast<std::int16_t>(msg);
        m.flags = 0;
        m.sender = -1;
        m.level = 0;
        if (kind == 0) {
            int p = suspect(plot(msg).suspect).person;
            m.sender = static_cast<std::int8_t>(p);
            m.senderCity = static_cast<std::int8_t>(person(p).city);
            m.level = person(p).fate;
            if (plot(msg).flags & 0xFFF) {
                int q = suspect(plot(msg).flags & 0xFF).person;
                m.receiver = static_cast<std::int8_t>(q);
                m.receiverCity = static_cast<std::int8_t>(person(q).city);
            }
        }
    }
    // (past 128 entries the original writes over face_shown, popup_flag and plots[])
    h.numMsgs = static_cast<std::int16_t>(h.numMsgs + 1);
    if (h.numMsgs >= 128) {
        g_.textBuf = "NSYN Overflow\n";
        notice(g_.textBuf, 0x32, 0x32);
    }
}

}  // namespace redux
