#include "game/EndCase.h"

#include <cstdlib>
#include <ctime>

#include "game/CaseGen.h"
#include "game/NewChar.h"
#include "game/Paper.h"
#include "game/Sim.h"

namespace reaction {

namespace {

using K = DrawOp::Kind;

// The original's int: arithmetic wraps at 16 bits.
std::int16_t i16(long v) { return static_cast<std::int16_t>(v); }

// 1 << n as MSC's "shl ax, cl" on a 386+ (count & 31; 16..31 give 0).
std::uint16_t shl16(int n) {
    n &= 31;
    return n < 16 ? static_cast<std::uint16_t>(1u << n) : 0;
}

const char* const kRankNames[8] = {"Recruit",   "Operative",     "Technician",   "Agent",
                                   "Organizer", "Special Agent", "Group Leader", "MasterMind"};

DrawOp op(K k, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0, int f = 0, std::string text = {}) {
    DrawOp o;
    o.kind = k;
    o.a = a; o.b = b; o.c = c; o.d = d; o.e = e; o.f = f;
    o.text = std::move(text);
    return o;
}
DrawOp text(const std::string& s, int x, int y, int color) { return op(K::Text, x, y, color, 0, 0, 0, s); }
DrawOp centered(const std::string& s, int x, int y, int color) {
    return op(K::TextCentered, x, y, color, 0, 0, 0, s);
}

std::string itoa(int v) { return std::to_string(v); }

}  // namespace

EndOfCase::EndOfCase(GameState& g, const GameData& data)
    : g_(g), sim_(std::make_unique<Sim>(g.world, g.rng, data, g.career)) {}

EndOfCase::~EndOfCase() = default;

std::string& EndOfCase::tb() { return sim_->globals().textBuf; }

void EndOfCase::startFinal(GameState& g) {
    auto seed = static_cast<std::uint32_t>(std::time(nullptr) & 0x7FFF);
    if (const char* s = std::getenv("COVERT_SEED")) seed = static_cast<std::uint32_t>(std::strtoul(s, nullptr, 0));
    g.rng.seed(seed);   // srand(rand_seed = ...); load_env then reloads rand_seed itself
    g.briefed = false;
    g.world.hdr.options = static_cast<std::int16_t>(g.world.hdr.options | 8);
}

std::string EndOfCase::introText() {
    std::string t = "It looks like that about wraps it up, Max. ";
    t += "We've been able to put together a pretty good chronology of this case, take a look.";
    wrapTextBuffer(t, 40);
    return t;
}

std::array<std::uint16_t, 16> EndOfCase::orgFlags() const {
    std::array<std::uint16_t, 16> f{};
    for (std::size_t i = 0; i < 16; ++i) f[i] = g_.world.orgs[i].flags;
    return f;
}

void EndOfCase::restoreOrgFlags(const std::array<std::uint16_t, 16>& f) {
    for (std::size_t i = 0; i < 16; ++i) w().orgs[i].flags = f[i];
}

// FINAL links the library's load_message_text, which doesn't mark the
// organization known as GAME's copy does.
void EndOfCase::loadMessageText(int p) {
    const auto f = orgFlags();
    sim_->loadMessageText(p);
    restoreOrgFlags(f);
}

void EndOfCase::truncate(int width) {
    auto& t = tb();
    while (!t.empty() && sim_->textWidth(t) > width) t.pop_back();
}

std::string EndOfCase::formatDateTime(int t) const {
    std::string s = "00:00 AM Jun 00";
    const int minute = t % 60;
    s[3] = static_cast<char>(minute / 10 + '0');
    s[4] = static_cast<char>(minute % 10 + '0');
    t /= 60;
    const int hour = t % 24;
    const int h = (hour - 1) % 12 + 1;
    s[0] = static_cast<char>(h / 10 + '0');
    s[1] = static_cast<char>(h % 10 + '0');
    s[6] = hour > 11 ? 'P' : 'A';
    s.resize(9);
    s += kMonthNames[static_cast<std::size_t>((g_.career.curMonth % 12 + 12) % 12)];
    s += " 10";
    const int day = t / 24 + 1;
    s[13] = static_cast<char>(day / 10 + '0');
    s[14] = static_cast<char>(day % 10 + '0');
    return s;
}

// ---------------------------------------------------------------- end_of_case

void EndOfCase::begin() {
    auto& h = w().hdr;
    numParticipants_ = h.numSuspects - h.difficulty * 4;
    for (int i = 0; i < h.numPersons && i < 64; ++i) {
        auto& p = w().persons[static_cast<std::size_t>(i)];
        p.points = static_cast<std::int16_t>(p.known);   // known_before
    }
    c().mm.known = w().persons[0].known;
    // reveal_all_case_data
    for (int i = 0; i < h.numPersons && i < 64; ++i) {
        auto& p = w().persons[static_cast<std::size_t>(i)];
        if (p.id) p.known = 0xFFFF;
    }
    for (int i = 0; i < h.numSuspects; ++i) {
        auto& s = sim_->suspect(i);
        s.flags |= 0x8000;
        sim_->person(s.person).roles |= shl16(i);
    }
    for (int i = 0; i < h.numMsgs && i < 128; ++i) w().msgs[static_cast<std::size_t>(i)].flags |= 0xF;
    for (int i = 0; i < h.numPlots; ++i) {
        auto& p = w().plots[static_cast<std::size_t>(i)];
        p.flags = static_cast<std::int16_t>(static_cast<std::uint16_t>(p.flags) | 0x8000);
    }
    w().persons[0].known = c().mm.known;
    caseResult_ = 0;
    epEarned_ = 0;
    itemsTaken_ = 0;
    epPossible_ = 0;
    caseItems_ = 0;
}

// ---------------------------------------------------------------- case_timeline

void EndOfCase::drawLabel(std::vector<DrawOp>& ops, int x, int y, int n) {
    const std::string name = sim_->suspect(n).name.str();
    const auto sp = name.find(' ');
    if (sp == std::string::npos) {
        ops.push_back(text(name, x + 2, y + 2, 5));
    } else {
        ops.push_back(text(name.substr(0, sp), x + 2, y + 2, 5));
        ops.push_back(text(name.substr(sp + 1), x + 2, y + 10, 5));
    }
}

void EndOfCase::drawPerson(std::vector<DrawOp>& ops, int n) {
    const int x = n % 4 * 80, y = n / 4 * 52;
    ops.push_back(op(K::Fill, x + 1, y + 1, 78, 50, 1));
    const int face = sim_->person(sim_->suspect(n).person).id;
    ops.push_back(op(K::FaceParts, face, x + 45, y + 8));
    ops.push_back(op(K::Sprite, ((face & 1) ? 0x68 : 0x70) + (face & 3), x + 35, y + 33));
    drawLabel(ops, x, y, n);
}

void EndOfCase::drawIcon(std::vector<DrawOp>& ops, int n) {
    const int x = n % 4 * 80, y = n / 4 * 52;
    ops.push_back(op(K::Recolor, x + 1, y + 1, 78, 50, 1, 9));
}

void EndOfCase::drawLink(std::vector<DrawOp>& ops, int person) {
    for (int i = 0; i < numParticipants_; ++i)
        if (sim_->suspect(i).person == person) drawIcon(ops, i);
}

void EndOfCase::scroll(std::vector<DrawOp>& ops, int person) {
    if (person == -1) return;
    for (int s = 0; s < numParticipants_; ++s) {
        if (sim_->suspect(s).person != person) continue;
        const int x = s % 4 * 80;
        int y = s / 4 * 52;
        ops.push_back(op(K::FillFromPixel, x + 1, y + 18, x + 1, y + 19, 16, 32));
        for (std::size_t k = 0; k < 4; ++k) {
            if (w().items[k].holder == person) {
                ops.push_back(op(K::Icon, w().items[k].icon, x + 1, y + 35));
                y -= 16;
            }
        }
    }
}

void EndOfCase::drawEvent(std::vector<DrawOp>& ops, int person) {
    for (int i = 0; i < numParticipants_; ++i) {
        if (sim_->suspect(i).person != person) continue;
        const int x = i % 4 * 80, y = i / 4 * 52;
        ops.push_back(op(K::Fill, x, y, 80, 11, 7));
        ops.push_back(op(K::Fill, x, y + 10, 24, 42, 7));
        ops.push_back(op(K::Fill, x + 72, y + 10, 8, 42, 7));
        ops.push_back(op(K::Fill, x, y + 40, 80, 12, 7));
        ops.push_back(op(K::Line, x + 24, y + 39, x + 71, y + 39, 8));
        int k = 24;
        do {
            ops.push_back(op(K::Line, x + k, y + 11, x + k, y + 39, 7));
            ops.push_back(op(K::Line, x + k + 1, y + 11, x + k + 1, y + 39, 0));
        } while ((k += 6) < 72);
        drawLabel(ops, x, y, i);
    }
}

void EndOfCase::drawMarker(std::vector<DrawOp>& ops, int person) {
    for (int i = 0; i < numParticipants_; ++i) {
        if (sim_->suspect(i).person != person) continue;
        const int x = i % 4 * 80, y = i / 4 * 52;
        for (int col = 2; col < 16; ++col) ops.push_back(op(K::Recolor, x + 1, y + 9, 78, 42, col, 0));
    }
}

void EndOfCase::drawPersonB(std::vector<DrawOp>& ops, int person) {
    for (int i = 0; i < numParticipants_; ++i)
        if (sim_->suspect(i).person == person) drawPerson(ops, i);
}

std::vector<Page> EndOfCase::timeline() {
    std::vector<Page> pages;
    Page cur;
    auto& ops = cur.ops;
    auto& t = tb();
    auto endPage = [&](Page::Wait wt) {
        cur.wait = wt;
        cur.caseItems = caseItems_;
        cur.itemsTaken = itemsTaken_;
        for (std::size_t k = 0; k < 4; ++k) cur.holders[k] = w().items[k].holder;
        pages.push_back(std::move(cur));
        cur = Page{};
    };
    auto orgCity = [&](int p) {
        t += w().orgs[static_cast<std::size_t>(sim_->person(p).org & 15)].name.str();
        t += "/";
        t += w().cities[static_cast<std::size_t>(sim_->person(p).city & 15)].name.str();
    };
    auto cityName = [&](int c) { return w().cities[static_cast<std::size_t>(c & 15)].name.str(); };

    ops.push_back(op(K::Fill, 0, 0, 320, 104, 7));
    for (int i = 0; i < numParticipants_; ++i) drawPerson(ops, i);
    drawMarker(ops, 0);
    ops.push_back(op(K::Recolor, 0, 0, 320, 104, 9, 7));
    ops.push_back(op(K::Fill, 0, 104, 320, 96, 1));
    for (auto& it : w().items) it.holder = -1;
    const int nm = w().hdr.numMsgs;
    for (int i = 0; i < nm && i < 128; ++i) {
        const Msg& m = w().msgs[static_cast<std::size_t>(i)];
        if (m.type == -1) continue;
        const int what = m.msg;
        int wait = 0;
        ops.push_back(op(K::Fill, 8, 112, 304, 80, 9));
        ops.push_back(op(K::Recolor, 0, 0, 320, 104, 9, 1));
        ops.push_back(centered(formatDateTime(m.time).substr(9), 160, 114, 7));
        switch (m.type) {
        case 0: {
            if (m.flags == 0) {
                wait = 1;
                break;
            }
            Plot& pl = sim_->plot(what);
            if (!(pl.flags & 0x100) || what < 0) {
                t.clear();
                int p = m.sender;
                sim_->appendPersonName(p);
                if (what >= 0) {
                    t += " (";
                    t += sim_->suspect(pl.suspect).name.str();
                    t += ") ";
                    drawIcon(ops, pl.suspect);
                } else {
                    drawLink(ops, p);
                    t += " ";
                }
                if (p) orgCity(p);
                truncate(300);
                ops.push_back(centered(t, 160, 124, 15));
                if (what < 0) {
                    t = " sent message to ";
                    sim_->appendPersonName(m.receiver);
                    ops.push_back(centered(t, 160, 132, 15));
                    ops.push_back(op(K::Line, 20, 150, 299, 150, 7));
                    t = "^";
                    loadMessageText(what);
                    t += "^";
                    ops.push_back(op(K::Wrapped, 35, 20, 152, 14, 0, 0, t));
                    drawLink(ops, m.receiver);
                } else {
                    t = pl.name.str();
                    const int fl = static_cast<std::uint16_t>(pl.flags);
                    if (fl & 0x200) t = "sent message to";
                    if (fl & 0x400) t = "sent package to";
                    ops.push_back(centered(t, 160, 132, 15));
                    if (fl & 0xF00) {
                        t.clear();
                        p = m.receiver;
                        sim_->appendPersonName(p);
                        t += " (";
                        t += sim_->suspect(fl & 0xFF).name.str();
                        t += ") ";
                        if (p) orgCity(p);
                        truncate(300);
                        ops.push_back(centered(t, 160, 140, 15));
                        drawIcon(ops, fl & 0xFF);
                        ops.push_back(op(K::Line, 20, 150, 299, 150, 7));
                        if (!(fl & 0x400)) {
                            t = "^";
                            loadMessageText(what);
                            t += "^";
                            ops.push_back(op(K::Wrapped, 35, 12, 152, 14, 0, 0, t));
                        }
                    }
                }
                wait = -1;
            } else {
                wait = 1;
            }
            const Plot& pl2 = sim_->plot(what);
            const int p = sim_->suspect(pl2.suspect).person;
            for (int item = 0; item < 4; ++item) {
                auto& it = w().items[static_cast<std::size_t>(item)];
                const auto items = static_cast<std::uint16_t>(pl2.items);
                if (items & (1u << item)) {
                    if (wait <= 0) endPage(Page::Wait::Key);
                    ops.push_back(op(K::Fill, 8, 112, 304, 80, 9));
                    const int holder = it.holder;
                    it.holder = static_cast<std::int8_t>(p);
                    scroll(ops, holder);
                    scroll(ops, p);
                    t.clear();
                    sim_->appendPersonName(p);
                    t += " (";
                    t += sim_->suspect(pl2.suspect).name.str();
                    t += ") ";
                    ops.push_back(centered(t, 160, 140, 15));
                    t = "now has the ";
                    t += it.name.str();
                    ops.push_back(centered(t, 160, 148, 15));
                    wait = -1;
                    caseItems_ |= static_cast<std::uint16_t>(1u << item);
                }
                if (items & (0x100u << item)) {
                    const int holder = it.holder;
                    it.holder = -1;
                    scroll(ops, holder);
                }
            }
            break;
        }
        case 11:
            wait = 1;
            break;
        case 1:
            t.clear();
            sim_->appendPersonName(what >> 8);
            t += " left ";
            t += cityName(what & 0xFF);
            break;
        case 4:
            t = "You traveled to ";
            t += cityName(what);
            break;
        case 5:
            t = "You broke in to ";
            t += w().orgs[static_cast<std::size_t>((what >> 8) & 15)].name.str();
            t += "/";
            t += cityName(what & 0xFF);
            break;
        case 6:
            t = "You arrested ";
            sim_->appendPersonName(what);
            ops.push_back(centered(t, 160, 144, 15));
            drawEvent(ops, what);
            wait = -1;
            break;
        case 7: {
            auto& it = w().items[static_cast<std::size_t>(what & 3)];
            t = "You confiscated ";
            t += it.name.str();
            const int holder = it.holder;
            it.holder = -1;
            scroll(ops, holder);
            itemsTaken_ |= shl16(what);
            break;
        }
        case 8: {
            t.clear();
            sim_->appendPersonName(what);
            t += " went into hiding.";
            ops.push_back(centered(t, 160, 140, 15));
            int holder = -1;
            for (int item = 0; item < w().hdr.numPlots; ++item) {
                const Plot& pl = w().plots[static_cast<std::size_t>(item)];
                if (pl.state == 999 && sim_->suspect(pl.suspect).person == what) {
                    holder = item;
                    break;
                }
            }
            if (holder == -1) {
                t = "Mission completed.";
            } else {
                t = "Unable to";
                ops.push_back(centered(t, 160, 148, 15));
                t = "complete mission.";
            }
            ops.push_back(centered(t, 160, 156, 15));
            drawMarker(ops, what);
            wait = -1;
            break;
        }
        case 9:
        case 10:
        case 13:
            t.clear();
            sim_->appendPersonName(what);
            if (m.type == 9) t += " came out of hiding.";
            else if (m.type == 10) t += " escaped from jail.";
            else t += " released by exchange.";
            ops.push_back(centered(t, 160, 144, 15));
            drawPersonB(ops, what);
            wait = -1;
            break;
        case 12:
            t = "You turned ";
            sim_->appendPersonName(what);
            drawLink(ops, what);
            ops.push_back(op(K::Recolor, 0, 0, 320, 104, 9, 2));
            break;
        default:   // text_buf as the last event left it
            break;
        }
        if (wait == 0) {
            truncate(300);
            ops.push_back(centered(t, 160, 144, 15));
        }
        if (wait <= 0) endPage(Page::Wait::KeyOrEsc);
    }
    endPage(Page::Wait::None);
    return pages;
}

void EndOfCase::timelineEscaped(const Page& p) {
    caseItems_ = p.caseItems;
    itemsTaken_ = p.itemsTaken;
    for (std::size_t k = 0; k < 4; ++k) w().items[k].holder = p.holders[k];
}

// ---------------------------------------------------------------- efficiency_report

std::vector<Page> EndOfCase::efficiencyReport() {
    std::vector<Page> pages;
    Page cur;
    auto& ops = cur.ops;
    auto& t = tb();
    auto card = [&] {
        pages.push_back(std::move(cur));
        cur = Page{};
    };
    ops.push_back(op(K::PageFrame, 14, 0, 0, 0, 0, 0, "Efficiency Report"));
    int ypos = 11, xpos = 8;
    for (int k = 0; k < numParticipants_; ++k) {
        ops.push_back(op(K::CardPanel, xpos, ypos));
        const Suspect& s = sim_->suspect(k);
        ops.push_back(text(s.name.str(), xpos + 100, ypos + 2, 0));
        const int p = s.person;
        const Person& pp = sim_->person(p);
        if (!(s.flags & 0x100) || pp.fate == -1 || pp.fate == -3) {
            ops.push_back(op(K::Portrait, pp.id, xpos + 0x66, ypos + 0x10));
            t.clear();
            sim_->appendPersonName(p);
            t += ", ";
            t += kRankNames[s.rank & 7];
            ops.push_back(text(t, xpos + 100, ypos + 0x48, 0));
            t = w().orgs[static_cast<std::size_t>(pp.org & 15)].name.str();
            t += "/";
            t += w().cities[static_cast<std::size_t>(pp.city & 15)].name.str();
            ops.push_back(text(t, xpos + 100, ypos + 0x50, 0));
        }
        t = "At Large";
        int score = 0;
        if (pp.fate == -1) {
            score = 5;
            t = "ARRESTED";
            ++caseResult_;
        } else if (pp.fate == -3) {
            score = 10;
            t = "TURNED";
            ++caseResult_;
        } else {
            const auto before = static_cast<std::uint8_t>(pp.points);   // known_before
            if ((before & 0xE) == 0xE) ++score;
            if (before & 1) ++score;
        }
        if (p == 0 && score >= 5) caseResult_ |= 0x80;
        ops.push_back(text(t, xpos + 0x18, ypos + 2, 12));
        score = i16(score * (s.rank + 3));
        int cost = i16(s.rank * 5 + 15);
        if (p == 0) {
            score = i16(score << 2);
            cost = i16(cost << 2);
        }
        ops.push_back(text("EP:" + itoa(score) + "\x89/" + itoa(cost), xpos + 0xF4, ypos + 2, 1));
        epEarned_ = i16(epEarned_ + score);
        epPossible_ = i16(epPossible_ + cost);
        xpos = rnd(14) + 2;
        ypos += 10;
        card();
    }
    for (int k = 0; k < 4; ++k) {
        if (!(caseItems_ & (1u << k))) continue;
        ops.push_back(op(K::CardPanel, xpos, ypos));
        epPossible_ = i16(epPossible_ + 50);
        ops.push_back(text(w().items[static_cast<std::size_t>(k)].name.str(), xpos + 100, ypos + 2, 0));
        t = "EP:0";
        if (itemsTaken_ & (1u << k)) {
            ops.push_back(text("CONFISCATED", xpos + 8, ypos + 2, 12));
            t = "EP:50";
            epEarned_ = i16(epEarned_ + 50);
        } else {
            ops.push_back(text("Not found", xpos + 0x18, ypos + 2, 12));
        }
        t += "\x89/50";
        ops.push_back(text(t, xpos + 0xF0, ypos + 2, 1));
        xpos = rnd(14) + 2;
        ypos += 10;
        card();
    }
    for (int k = 0; k < w().hdr.numMsgs && k < 128; ++k) {
        const Msg& m = w().msgs[static_cast<std::size_t>(k)];
        if (m.type != 0) continue;
        const int msg = m.msg;
        const Plot& pl = sim_->plot(msg);
        if (!pl.severity) continue;
        ops.push_back(op(K::CardPanel, xpos, ypos));
        if (pl.severity >= 5) caseResult_ |= 0x40;
        const int score = i16(pl.severity * 40);
        t = pl.name.str();
        if (!(pl.flags & 0x2000)) {
            t += " ";
            t += sim_->suspect(pl.suspect).name.str();
        }
        ops.push_back(centered(t, xpos + 0x97, ypos + 2, 0));
        ops.push_back(text("EP: 0/" + itoa(score), xpos + 0xF0, ypos + 2, 4));
        epPossible_ = i16(epPossible_ + score);
        t.clear();
        loadMessageText(msg);
        ops.push_back(op(K::Wrapped, 0x20, xpos + 8, ypos + 0x10, 12, 0, 0, t));
        xpos = rnd(14) + 2;
        ypos += 10;
        card();
    }
    if (!(caseResult_ & 0x40)) {
        ops.push_back(op(K::CardPanel, xpos, ypos));
        ops.push_back(centered("Prevented crime", xpos + 0x97, ypos + 2, 0));
        ops.push_back(text("EP:100\x89/100", xpos + 0xF0, ypos + 2, 1));
        epEarned_ = i16(epEarned_ + 100);
        epPossible_ = i16(epPossible_ + 100);
        xpos = rnd(14) + 2;
        ypos += 10;
        card();
    }
    int moles = 0;
    for (int k = 0; k < 16; ++k)
        if (static_cast<std::uint16_t>(w().hdr.doubleAgents) & (1u << k)) ++moles;
    if (moles) {
        ops.push_back(op(K::CardPanel, xpos, ypos));
        ops.push_back(text(itoa(moles) + " Double Agent(s)", xpos + 100, ypos + 2, 0));
        ops.push_back(text("remained undetected", xpos + 100, ypos + 10, 0));
        ops.push_back(text("within the CIA.", xpos + 100, ypos + 0x12, 0));
        ops.push_back(text("EP:0/" + itoa(moles * 50), xpos + 0xF0, ypos + 2, 4));
        epPossible_ = i16(epPossible_ + moles * 50);
        xpos = rnd(14) + 2;
        ypos += 10;
        card();
    }
    ops.push_back(op(K::CardPanel, xpos, ypos));
    const int div = epPossible_ / 40;
    int total = div ? i16(i16(epEarned_ * 25) / div) : 0;
    if (total < 0) total = 0;
    ops.push_back(text("Efficiency Rating", xpos + 100, ypos + 2, 0));
    ops.push_back(text("--- " + itoa(total) + " ---", xpos + 100, ypos + 0x18, 0));
    ops.push_back(text("EP:" + itoa(epEarned_) + "\x89/" + itoa(epPossible_), xpos + 0xF0, ypos + 2, 1));
    card();
    ep_ = total;
    return pages;
}

// ---------------------------------------------------------------- the story and the clue

std::string EndOfCase::storyKey() const {
    std::string k = "*PL0000";
    const int pn = g_.career.plotNum;
    k[3] = static_cast<char>(pn / 10 + '0');
    k[4] = static_cast<char>(pn % 10 + '0');
    k[5] = static_cast<char>(g_.career.lastEpisode + '0');
    if (caseResult_ & 0x40) k[6] = '5';
    return k;
}

std::optional<std::string> EndOfCase::afterStory() {
    clue_ = -1;
    if (caseResult_ & 0x40) {
        c().plotEpisodes = static_cast<std::uint16_t>(c().plotEpisodes | shl16(c().lastEpisode));
        return std::nullopt;
    }
    const unsigned k = c().mm.known;
    int clue = -1;
    if (!(k & 8)) clue = 8;
    if (!(k & 4)) clue = 4;
    if (!(k & 1)) clue = 1;
    if (!(k & 2)) clue = 2;
    if (clue == -1 || !c().plotNum) return std::nullopt;
    clue_ = clue;
    std::string t = "Interrogation of the suspects has revealed some additional information about Mr.Big.";
    wrapTextBuffer(t, 40);
    return t;
}

// suspect_dossier(who, 0, 0).
std::vector<Page> EndOfCase::dossier(int who) {
    std::vector<Page> pages;
    Page cur;
    auto& ops = cur.ops;
    auto& t = tb();
    Person& p = sim_->person(who);
    const int x = 10;
    t.clear();
    sim_->appendPersonName(who);
    ops.push_back(op(K::PageFrame, 3, 0, 0, 0, 0, 0, t));
    int y = 18;
    y += 2;
    t = "Name:\x80";
    sim_->appendPersonName(who);
    ops.push_back(text(t, x, y, 7));
    if (p.known & 0x100) {
        int rank = (p.id >> 2) & 7;
        for (int ei = 0; ei < w().hdr.numSuspects; ++ei)
            if (sim_->suspect(ei).person == who) rank = sim_->suspect(ei).rank;
        t = "Rank:\x80";
        t += kRankNames[rank & 7];
        y += 8;
        ops.push_back(text(t, x, y, 7));
    }
    if (p.known & 4) {
        w().orgs[static_cast<std::size_t>(p.org & 15)].flags |= 1;
        t = "Org: \x80";
        t += w().orgs[static_cast<std::size_t>(p.org & 15)].name.str();
        y += 8;
        ops.push_back(text(t, x, y, 7));
    }
    if (p.known & 8) {
        t = "City: \x80";
        t += w().cities[static_cast<std::size_t>(p.city & 15)].name.str();
        y += 8;
        ops.push_back(text(t, x, y, 7));
    }
    t = "Recruited";
    if (p.known & 0x400) {
        for (int ei = 0; ei < w().hdr.numSuspects; ++ei) {
            const int sp = sim_->suspect(ei).person;
            if (p.org == sim_->person(sp).org) {
                if (who != sp) {
                    t += " by ";
                    sim_->personName(sp);
                }
                break;
            }
        }
    }
    if (p.known & 0x200) {
        int nearCity = -1, dist = 999, ei = 0;
        do {
            if (ei != p.city && sim_->cityMapDistance(ei, p.city) < dist) {
                nearCity = ei;
                dist = sim_->cityMapDistance(ei, p.city);
            }
        } while (++ei < 16);
        if (nearCity != -1) {
            t += " in ";
            t += w().cities[static_cast<std::size_t>(nearCity)].name.str();
        }
    }
    if (t.size() > 12) {
        t += ".";
        y += 8;
        ops.push_back(text(t, x, y, 7));
    }
    if (p.known & 1) {
        ops.push_back(op(K::Portrait, p.id, 264, 8));
        ops.push_back(op(K::Sprite, 108, 299, 40));
    }
    bool role = false;
    y += 10;
    ops.push_back(op(K::Line, x, y - 2, x + 128, y - 2, 12));
    for (int ei = 0; ei < w().hdr.numSuspects; ++ei) {
        if (p.roles & shl16(ei)) {
            t = "Role: \x80";
            t += sim_->suspect(ei).name.str();
            ops.push_back(text(t, x, y, 7));
            role = true;
        }
    }
    if (!role) {
        if ((p.known & 0x1F) == 0x1F && !(p.flags & 0x7FFF)) t = "Role: \x80NOT INVOLVED";
        else t = "Role: \x80unknown";
        ops.push_back(text(t, x, y, 7));
    }
    const char* fate = p.fate == -1 ? "(Under Arrest)" : p.fate == -2 ? "(In Hiding)" : p.fate == -3 ? "(Turned)" : nullptr;
    if (fate) {
        y += 8;
        ops.push_back(text(fate, x, y, 5));
    }
    ops.push_back(op(K::Line, x, y + 8, x + 128, y + 8, 12));
    y += 2;
    y = clampTo(y + 8, (p.known & 1) ? 54 : 0, 99);
    for (int ei = 0; ei < w().hdr.numMsgs && ei < 128; ++ei) {
        Msg& m = w().msgs[static_cast<std::size_t>(ei)];
        if (m.sender != who || !m.flags) continue;
        for (int guard = 0; guard < 8; ++guard) {
            t.clear();
            sim_->formatMessageEvent(ei);
            ops.push_back(op(K::Wrapped, 37, x, y, 5, 0, 0, t));
            y = sim_->drawWrappedText(37, y);
            const int msg = m.msg;
            if (m.flags & 0x8000) {
                t = "'";
                loadMessageText(msg);
                t += "'";
                ops.push_back(op(K::Wrapped, 36, x + 4, y, 12, 0, 0, t));
                y = sim_->drawWrappedText(36, y);
            } else {
                const int kind = static_cast<std::uint16_t>(sim_->plot(msg).flags) & 0xF00;
                if (kind == 0x200 || kind == 0x300) {
                    ops.push_back(op(K::TextOpaque, x + 4, y, 4, 15, 0, 0, "(message not decoded)"));
                    y += 8;
                }
                if (kind == 0x800 || kind == 0x900) {
                    t.clear();
                    loadMessageText(msg);
                    sim_->garbleMessage(m.flags, 5);
                    ops.push_back(op(K::Wrapped, 36, x + 4, y, 12, 0, 0, t));
                    y = sim_->drawWrappedText(36, y);
                }
            }
            if (y < 192) break;
            ops.push_back(op(K::TextOpaque, 256, 192, 12, 15, 0, 0, "...more"));
            pages.push_back(std::move(cur));
            cur = Page{};
            t.clear();
            sim_->appendPersonName(who);
            cur.ops.push_back(op(K::PageFrame, 3, 0, 0, 0, 0, 0, t));
            y = 16;
        }
    }
    cur.wait = Page::Wait::None;
    pages.push_back(std::move(cur));
    return pages;
}

void EndOfCase::describeClue(int pi, unsigned newk, std::vector<ClueStep>& out) {
    Person& p = sim_->person(pi);
    unsigned old = p.known;
    p.known = static_cast<std::uint16_t>(newk);
    if (old == newk) return;
    auto& t = tb();
    auto notice = [&](ClueStep& s) { s.notice = t; };
    for (int guard = 0; guard < 64; ++guard) {
        ClueStep s;
        s.dossier = dossier(pi);
        old |= p.known;
        const unsigned diff = old ^ p.known;
        if (diff & 0x10) {
            p.known |= 0x10;
            t.clear();
            sim_->appendPersonName(pi);
            t += " has been\nidentified as a participant\nin the plot.\n";
            notice(s);
        } else if (diff & 2) {
            t.clear();
            sim_->appendPersonName(pi);
            t += " has been positively\nidentified as ";
            p.known |= 2;
            sim_->appendPersonName(pi);
            t += "\n";
            notice(s);
        } else if (diff & 1) {
            p.known |= 1;
            t = "We have acquired a\nphotograph of ";
            sim_->appendPersonName(pi);
            t += ".\n";
            notice(s);
        } else if (diff & 8) {
            p.known |= 8;
            t.clear();
            sim_->appendPersonName(pi);
            t += " has been\nspotted in ";
            t += w().cities[static_cast<std::size_t>(p.city & 15)].name.str();
            t += "\n";
            notice(s);
            ++sim_->cityClueCount(p.city & 31);
        } else if (diff & 4) {
            p.known |= 4;
            t.clear();
            sim_->appendPersonName(pi);
            t += " has been\nidentified as a member\nof the ";
            t += w().orgs[static_cast<std::size_t>(p.org & 15)].name.str();
            t += "\n";
            notice(s);
            w().orgs[static_cast<std::size_t>(p.org & 15)].flags |= 1;
            ++sim_->orgClueCount(p.org & 15);
        } else if (diff & 0x20) {
            p.known |= 0x20;
            t = "We have obtained\nadditional information\nconcerning ";
            sim_->appendPersonName(pi);
            t += "\n";
            notice(s);
        } else if (diff & 0x100) {
            p.known |= 0x100;
            t = "We have determined\nthe rank of\n";
            sim_->appendPersonName(pi);
            t += "\n";
            notice(s);
        } else if (diff & 0x600) {
            p.known |= (diff & 0x200) ? 0x200 : 0x400;
            t = "We found recruiting\ninformation about\n";
            sim_->appendPersonName(pi);
            t += "\n";
            notice(s);
        }
        out.push_back(std::move(s));
        if (p.known == static_cast<std::uint16_t>(old)) break;
        // (the original loops for ever on bits it has no news for; the port stops)
        if (!(diff & 0x73F)) break;
    }
    ClueStep last;
    last.dossier = dossier(pi);
    last.dossier.back().wait = Page::Wait::Key;
    out.push_back(std::move(last));
}

std::vector<ClueStep> EndOfCase::mastermindClue() {
    std::vector<ClueStep> out;
    if (clue_ == -1) return out;
    // pick_clue(mm_org, mm_city, clue, 1)
    const int org = c().mm.org, city = c().mm.city;
    unsigned known = static_cast<unsigned>(clue_);
    auto& persons = w().persons;
    const int np = w().hdr.numPersons;
    bool found = false;
    for (int i = 0; i < np && i < 64; ++i) {
        Person& p = persons[static_cast<std::size_t>(i)];
        if (p.org == org && p.city == city) {
            const unsigned old = p.known;
            p.known = static_cast<std::uint16_t>(p.known | known);
            if (p.known) describeClue(i, old, out);
            found = true;
            break;
        }
    }
    if (!found) {
        int i = 0;
        do i = rnd(63) + 1;
        while (persons[static_cast<std::size_t>(i)].id);
        if (org == c().mm.org && city == c().mm.city) {
            i = 0;
            known |= c().mm.known;
        }
        if (known) {
            Person& p = persons[static_cast<std::size_t>(i)];
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
                for (int j = 0; j < np && j < 64; ++j) {
                    // (persons[i].id & 0x3f == persons[j].id & 0x3f): == binds first
                    const int pj = persons[static_cast<std::size_t>(j)].id;
                    if ((p.id & (0x3F == pj) & 0x3F) && i != j) ok = false;
                }
            } while (!ok);
            describeClue(i, old, out);
        }
    }
    c().mm.known = static_cast<std::uint16_t>(c().mm.known | static_cast<unsigned>(clue_));
    return out;
}

std::optional<std::string> EndOfCase::capturedText() const {
    if (g_.world.persons[0].fate != -1) return std::nullopt;
    std::string t = "You have captured the MasterMind controlling the ";
    t += g_.world.orgs[static_cast<std::size_t>(g_.career.mm.org & 15)].name.str();
    t += "! The country has been saved from a dangerous plot. ";
    t += "Congratulations, the President commends you.\n";
    wrapTextBuffer(t, 38);
    return t;
}

int EndOfCase::vacationIndex() const {
    const int d = g_.world.hdr.difficulty;
    return clampTo(i16(ep_ * (d + 1)) / 150, g_.career.curMonth != 0, 20);
}

bool EndOfCase::vacationExtra(int kind) { return kind <= 1 && rnd(3); }

// ---------------------------------------------------------------- the career

void EndOfCase::recordCase() {
    auto& cr = c();
    cr.curMonth = i16(cr.curMonth + 1);
    int len = -1;
    for (int i = 0; i < 0x1FC; ++i) {
        if (cr.history[static_cast<std::size_t>(i)] == 0xFF) {
            len = i;
            break;
        }
    }
    if (len != -1) {
        auto put = [&](int v) { cr.history[static_cast<std::size_t>(len++)] = static_cast<std::uint8_t>(v); };
        put((cr.plotNum << 4) + cr.lastEpisode);
        put(static_cast<int>(caseResult_));
        put(ep_ / 256);
        put(ep_);
        if (caseResult_ & 0x80) put(cr.leaderCode);
        cr.history[static_cast<std::size_t>(len)] = 0xFF;
    }
    if (caseResult_ & 0x80) cr.mastermindsArrested |= 1u << (cr.leaderCode & 31);
}

std::vector<Page> EndOfCase::careerSummary() {
    std::vector<Page> pages;
    Page cur;
    auto& ops = cur.ops;
    const auto& cr = c();
    auto opaque = [&](const std::string& s, int x, int y, int color) {
        ops.push_back(op(K::TextOpaque, x, y, color, 15, 0, 0, s));
    };
    auto frame = [&] {
        ops.push_back(op(K::PageFrame, 1, 0, 0, 0, 0, 0, "Career"));
    };
    auto waitKey = [&] {
        pages.push_back(std::move(cur));
        cur = Page{};
    };
    auto& t = tb();
    frame();
    ops.push_back(centered("The Career of", 160, 16, 0));
    ops.push_back(centered("Max '" + cr.codename.str() + "' Remington", 160, 24, 0));
    int off = 0, n = 0, arrests = 0, busted = 0, ypos = 0x24;
    while (off < 0x100 && cr.history[static_cast<std::size_t>(off)] != 0xFF) {
        const auto h = [&](int i) { return static_cast<int>(cr.history[static_cast<std::size_t>(off + i)]); };
        const int plot = h(0) >> 4, sub = h(0) & 0xF;
        t = "Case #" + itoa(n + 1) + ", " + kMonthNames[static_cast<std::size_t>(n % 12)] + " " + itoa(n / 12 + 1990) +
            ".  Arrests: ";
        int value = h(1) & 0xF;
        t += itoa(value);
        arrests += value;
        t += " EP: ";
        value = i16((h(2) << 8) + h(3));
        t += itoa(value);
        opaque(t, 16, ypos, 0);
        ypos += 8;
        t = (h(1) & 0x40) ? "... couldn't stop " : "... prevented ";
        const PlotDef& pd = kPlotDefs[static_cast<std::size_t>(plot & 15)];
        const int crime = sub < 8 ? pd.crime[static_cast<std::size_t>(sub)] : -1;
        t += crime >= 0 && crime < 13 ? kCrimeNames[static_cast<std::size_t>(crime)] : "?";
        t += "/";
        const char* role = sub < 8 ? pd.roles[static_cast<std::size_t>(sub)][0] : nullptr;
        t += role ? role : "?";
        truncate(300);
        opaque(t, 12, ypos, 7);
        ypos += 8;
        if (h(1) & 0x80) {
            const int leader = h(4);
            t = "... arrested ";
            t += leader < 26 ? kMastermindNames[static_cast<std::size_t>(leader)] : "?";
            t += " MasterMind!";
            ++off;
            opaque(t, 12, ypos, 12);
            ypos += 8;
            ++busted;
        }
        ypos += 2;
        off += 4;
        ++n;
        if (ypos > 0xB4) {
            waitKey();
            frame();
            ypos = 16;
        }
    }
    ops.push_back(op(K::Line, 12, ypos + 1, 0x12F, ypos + 1, 0));
    opaque("Arrested, MasterMinds: " + itoa(busted) + "  Agents: " + itoa(arrests), 12, ypos + 4, 0);
    waitKey();
    if (cr.mastermindsArrested) {
        int xpos = 999;
        ypos = 999;
        for (int i = 0; i < 26; ++i) {
            if (!(cr.mastermindsArrested & (1u << i))) continue;
            ++numMasterminds_;
            if (xpos > 0x100) {
                xpos = 0x1C;
                ypos += 0x50;
            }
            if (ypos > 0x8C) {
                if (ypos < 999) waitKey();
                frame();
                ops.push_back(centered("MasterMinds Arrested", 160, 16, 0));
                ypos = 32;
            }
            ops.push_back(op(K::FaceFor, i, xpos, ypos));
            ops.push_back(centered(kMastermindNames[static_cast<std::size_t>(i)], xpos + 0x19, ypos + 0x38, 12));
            xpos += 0x48;
            ypos ^= 8;
        }
        waitKey();
    }
    return pages;
}

std::optional<std::string> EndOfCase::promotion() {
    auto& cr = c();
    const int d = w().hdr.difficulty;
    cr.careerPoints = i16(cr.careerPoints + i16(ep_ * (d + 1)) / 16);
    const int den = i16(cr.careerPoints + 100);
    const int rank = clampTo(den ? i16(cr.curMonth * 100 + 10000) / den : 99, 1, 99);
    if (rank >= cr.careerRank) return std::nullopt;
    std::string t = "Congratulations Max, we've decided to promote you from 00";
    t += itoa(cr.careerRank);
    t += " rank to 00";
    t += itoa(rank);
    t += " rank. Here's a commendation from the President.";
    cr.careerRank = static_cast<std::int16_t>(rank);
    wrapTextBuffer(t, 36);
    return t;
}

std::optional<std::string> EndOfCase::retirement() const {
    const bool all = numMasterminds_ == 26, ten = g_.career.curMonth == 120;
    if (!all && !ten) return std::nullopt;
    std::string t;
    if (all) t = "INCREDIBLE: You have\narrested all 26 Masterminds!\n";
    if (ten) t = "You have completed a\nten year career with the CIA.\n";
    t += "You retire to a well-\ndeserved life of relaxation.\n";
    return t;
}

int EndOfCase::rogueMenuX() const { return ((g_.world.hdr.options & 2) ? 0 : 0x61) + 0x18; }

std::vector<Page> EndOfCase::roguesGallery() {
    std::vector<Page> pages;
    Page cur;
    const bool maxine = (w().hdr.options & 2) != 0;
    const int xo = maxine ? 0 : 0x61;
    cur.ops.push_back(op(K::Picture, 0, 0, 0, 0, 0, 0, "gender.pic"));
    cur.ops.push_back(op(K::Fill, xo, 0, 0xDF, 200, 5));
    const int x0 = maxine ? 0x104 : 0x38;
    cur.ops.push_back(centered("Max", x0, 0x56, 14));
    cur.ops.push_back(centered(c().codename.str(), x0, 0x5E, 14));
    cur.ops.push_back(centered("Remington", x0, 0x66, 14));
    for (int i = 0; i < 26; ++i) {
        if (!(c().mastermindsArrested & (1u << i))) continue;
        const int x = rnd(0xA4) + 4;
        const int y = rnd(0x8C) + 4;
        cur.ops.push_back(op(K::FaceFor, i, x + xo, y));
        cur.ops.push_back(text("ARRESTED", x + xo + 1, y + 0x18, 0));
        cur.ops.push_back(text("ARRESTED", x + xo, y + 0x17, 12));
        cur.wait = Page::Wait::Retrace;
        cur.retraces = 30;
        pages.push_back(std::move(cur));
        cur = Page{};
    }
    cur.wait = Page::Wait::Retrace;
    cur.retraces = 120;
    pages.push_back(std::move(cur));
    return pages;
}

std::string EndOfCase::continueMenu() const {
    std::string t = "Do you want to...\n Continue Game\n Save Game\n End Game\n";
    if (g_.world.hdr.difficulty < 3 && (caseResult_ & 0x80)) t += " Change Difficulty Level\n";
    return t;
}

std::string EndOfCase::difficultyMenu() {
    return "Which difficulty level?\n Local Disturbance\n National Threat\n Regional Conflict\n Global Crisis\n";
}

void EndOfCase::changeDifficulty(int r) { w().hdr.difficulty = static_cast<std::int16_t>(clampTo(r, 0, 3)); }

void EndOfCase::beforeSave() {
    auto& h = w().hdr;
    h.options = static_cast<std::int16_t>(h.options | 0x40);
    h.clockBase = 0;
    h.hereCity = 1;
}

// ---------------------------------------------------------------- a finished case for testing

void makeDemoFinishedCase(GameState& g, const GameData& data, std::uint32_t seed, int difficulty, int days) {
    g = GameState{};
    g.rng.seed(seed);
    g.world.hdr.randSeed = static_cast<std::int16_t>(seed & 0x7FFF);
    // FINAL's tables start as its BSS: zero.
    for (auto& m : g.world.msgs) m = Msg{0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (auto& cl : g.world.clues) cl.org = 0;
    startNewCharacter(g);
    chooseGender(g, 0);
    g.career.codename.set("Tester");
    chooseDifficulty(g, difficulty);
    std::array<int, 4> lvl{1, 1, 1, 1};
    finishTraining(g, lvl, 4);
    choosePlot(g, data);
    EpisodeChoice ep = chooseEpisode(g);
    while (!ep.ok) {
        choosePlot(g, data);
        ep = chooseEpisode(g);
    }
    startCaseClock(g);
    CaseResult res = CaseGenerator(g.world, g.rng, data, caseSetupFor(g)).generate();
    if (res.faceCheck) clearedForDuty(g.world);
    g.storeCareer();

    Sim sim(g.world, g.rng, data, g.career);
    sim.startNewGame();
    auto& w = g.world;
    int arrested = 0;
    for (int d = 0; d < days && !sim.caseOver(); ++d) {
        if (d == 2 || d == 5) {
            // Arrest a participant (not the MasterMind).
            for (int i = 1; i < w.hdr.numSuspects - w.hdr.difficulty * 4; ++i) {
                const int p = w.suspects[static_cast<std::size_t>(i)].person;
                if (p > 0 && p < 64 && w.persons[static_cast<std::size_t>(p)].fate == 0 && i % 3 == d % 3) {
                    w.persons[static_cast<std::size_t>(p)].fate = -1;
                    sim.addChronologyEntry(6, p);
                    ++arrested;
                    break;
                }
            }
        }
        if (d == 3) sim.travelTo((w.hdr.hereCity + 3) & 15);
        sim.advanceTime(1440);
        sim.takeEvents();
    }
    (void)arrested;
    g.career.mm.known = w.persons[0].known;
    g.storeCareer();
}

}  // namespace reaction
