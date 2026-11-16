// TAC's case hooks. See TacCase.h.
#include "game/TacCase.h"

#include <utility>

#include "formats/TextWindow.h"
#include "game/Paper.h"

namespace redux {

namespace {

Person toWorld(const TacSetup::Person& t) {
    Person p;
    p.org = static_cast<std::int16_t>(t.org);
    p.id = static_cast<std::int16_t>(t.id);
    p.city = static_cast<std::int16_t>(t.city);
    p.known = static_cast<std::uint16_t>(t.known);
    p.flags = static_cast<std::uint16_t>(t.clues);
    p.roles = static_cast<std::uint16_t>(t.found);
    p.points = static_cast<std::int16_t>(t.points);
    p.fate = static_cast<std::int16_t>(t.status);
    return p;
}

TacSetup::Person toTac(const Person& p) {
    TacSetup::Person t;
    t.org = p.org;
    t.id = p.id;
    t.city = p.city;
    t.known = p.known;
    t.clues = p.flags;
    t.found = p.roles;
    t.points = p.points;
    t.status = p.fate;
    return t;
}

std::string oneLine(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '\n' ? '|' : c;
    return o;
}

}  // namespace

// ---------------------------------------------------------------- the screen

// The library's drawing calls and waits, recorded as Tac::LibSteps.
class TacCaseHooks::Ui : public SimUi {
public:
    explicit Ui(TacCaseHooks& h) : h_(h) {}

    int event(const SimEvent& e) override {
        using K = SimEvent::Kind;
        switch (e.kind) {
        case K::Notice:
            h_.trace.push_back("notice " + oneLine(e.text));
            step(Tac::LibStep::Kind::Menu, e.text, e.x, e.y);
            break;
        case K::Wait: step(Tac::LibStep::Kind::Key); break;
        case K::Popup:
            // TAC's draw_intercept_popup; drawing the portrait clears face_shown
            h_.trace.push_back("popup " + std::to_string(e.index) + " " + oneLine(e.text));
            step(Tac::LibStep::Kind::Popup, e.text, 0, 0, e.index);
            if (e.index > -1) h_.sim_->globals().faceShown = -1;
            break;
        case K::Dossier: h_.trace.push_back("dossier " + std::to_string(e.index)); break;
        case K::Clue:
            h_.trace.push_back(std::string(e.title.empty() ? "clue " : "  related ") + std::to_string(e.index) + " " +
                               oneLine(e.text));
            break;
        case K::NewInfo: h_.trace.push_back("newinfo " + std::to_string(e.index) + " " + oneLine(e.text)); break;
        default: break;
        }
        return -1;
    }
    void retrace(int n) override { step(Tac::LibStep::Kind::Retrace, {}, 0, 0, n); }
    void pageFrame(const std::string& title, int color) override {
        draw([title, color](Tac& t) {
            drawPageFrame(t.gpage(), *t.gfx_.font, t.faces_, title, static_cast<std::uint8_t>(color));
            t.gfx_.fillcolor = 15;
        });
    }
    void text(const std::string& s, int x, int y, int c) override {
        draw([s, x, y, c](Tac& t) { t.gfx_.textTransparent(s, x, y, c); });
    }
    void textOpaque(const std::string& s, int x, int y, int c) override {
        draw([s, x, y, c](Tac& t) { t.gfx_.text(s, x, y, c); });
    }
    void line(int x1, int y1, int x2, int y2, int c) override {
        draw([=](Tac& t) { t.gfx_.line(x1, y1, x2, y2, c); });
    }
    void wrapped(const std::string& s, int width, int x, int y, int c) override {
        draw([s, width, x, y, c](Tac& t) {
            drawWrappedText(t.gpage(), *t.gfx_.font, s, width, x, y, static_cast<std::uint8_t>(c));
        });
    }
    void portrait(int face, int x, int y) override {
        draw([=](Tac& t) { drawPortrait(t.gpage(), t.faces_, face, x, y); });
        h_.sim_->globals().faceShown = -1;   // draw_face_parts
    }
    void sprite(int n, int x, int y) override {
        draw([=](Tac& t) { t.faces_.draw(t.gpage(), n, x, y); });
    }
    void cardPanel(int x, int y) override {
        draw([=](Tac& t) { drawCardPanel(t.gpage(), x, y); });
    }
    void bevelBox(int x, int y, int w, int hh, int c) override {
        draw([=](Tac& t) {
            drawBevelBox(t.gpage(), x, y, w, hh, static_cast<std::uint8_t>(c));
            t.gfx_.fillcolor = c;
        });
    }
    void fillRect(int x, int y, int w, int hh, int c) override {
        draw([=](Tac& t) { t.gfx_.fillRect(t.gfx_.gpage, x, y, w, hh, c); });
    }
    // (the master plan's draw_text_centered)
    void textCentered(const std::string& s, int x, int y, int c) {
        draw([s, x, y, c](Tac& t) { t.gfx_.textCentered(s, x, y, c); });
    }

    void step(Tac::LibStep::Kind k, std::string text = {}, int x = 0, int y = 0, int n = 0) {
        Tac::LibStep s;
        s.kind = k;
        s.text = std::move(text);
        s.x = x;
        s.y = y;
        s.n = n;
        h_.tac_->libSteps.push_back(std::move(s));
    }
    void draw(std::function<void(Tac&)> f) {
        Tac::LibStep s;
        s.draw = std::move(f);
        h_.tac_->libSteps.push_back(std::move(s));
    }

private:
    TacCaseHooks& h_;
};

// msgs[-1] and msgs[128] in TAC's DGROUP (msgs at A1E6h): msgs[-1] is
// room_noise[21..23], doors_open (.flags), show_ok, num_persons and tac_mode
// (.receiver, .receiver_city); msgs[128] (A8E6h) face_shown, throw_delay (.time),
// bugs_planted, frame_count (.flags), popup_flag and plots[0]'s first bytes
// (.receiver_city: the high byte of plots[0].state).
class TacCaseHooks::Neighbours : public SimNeighbours {
public:
    explicit Neighbours(TacCaseHooks& h) : h_(h) {}
    std::uint16_t msgFlags(int i) override {
        Tac& t = *h_.tac_;
        if (i == -1) return static_cast<std::uint16_t>(t.doors_open);
        if (i == 128) return static_cast<std::uint16_t>(t.frame_count);
        return 0;
    }
    void orMsgFlags(int i, unsigned v) override {
        Tac& t = *h_.tac_;
        if (i == -1) t.doors_open = static_cast<std::int16_t>(static_cast<std::uint16_t>(t.doors_open) | v);
        if (i == 128) t.frame_count = static_cast<std::int16_t>(static_cast<std::uint16_t>(t.frame_count) | v);
    }
    std::int16_t msgTime(int i) override {
        Tac& t = *h_.tac_;
        if (i == -1) return static_cast<std::int16_t>(t.room_noise[22]);
        if (i == 128) return static_cast<std::int16_t>(t.throw_delay);
        return 0;
    }
    int msgReceiverCity(int i) override {
        Tac& t = *h_.tac_;
        if (i == -1) return static_cast<std::int8_t>(static_cast<std::uint16_t>(t.tac_mode) >> 8);
        if (i == 128 && !h_.w_.plots.empty())
            return static_cast<std::int8_t>(static_cast<std::uint16_t>(h_.w_.plots[0].state) >> 8);
        return 0;
    }

private:
    TacCaseHooks& h_;
};

// ---------------------------------------------------------------- binding

struct TacCaseHooks::Bind {
    TacCaseHooks& h;
    Tac& t;
    Bind(TacCaseHooks& hh, Tac& tt) : h(hh), t(tt) { h.push(t); }
    ~Bind() { h.pull(t); }
    Sim& sim() { return *h.sim_; }
};

TacCaseHooks::TacCaseHooks(World& world, const GameData& data, Career& career)
    : w_(world), data_(data), career_(career), ui_(std::make_unique<Ui>(*this)),
      nb_(std::make_unique<Neighbours>(*this)) {}

TacCaseHooks::~TacCaseHooks() = default;

void TacCaseHooks::push(Tac& t) {
    tac_ = &t;
    if (!sim_) {
        sim_ = std::make_unique<Sim>(w_, t.rng_, data_, career_);
        sim_->setUi(ui_.get());
        // persons[-1]: the end of work_rp {3, 0, 0, 319, 199, 0, 15, 4, 0} and work_page (06EAh)
        Person m1;
        m1.org = 0;
        m1.id = 319;
        m1.city = 199;
        m1.known = 0;
        m1.flags = 15;
        m1.roles = 4;
        m1.points = 0;
        m1.fate = 0x06EA;
        sim_->setTacLibrary(m1, nb_.get());
    }
    for (std::size_t i = 0; i < t.persons.size() && i < 64; ++i) w_.persons[i] = toWorld(t.persons[i]);
    auto& h = w_.hdr;
    gameDifficulty_ = h.difficulty;
    h.difficulty = static_cast<std::int16_t>(t.difficulty);
    h.seenOrgs = static_cast<std::int16_t>(t.known_plans);
    h.hereOrg = static_cast<std::int16_t>(t.cur_org);
    h.hereCity = static_cast<std::int16_t>(t.cur_city);
    w_.cityClueCount[0] = static_cast<std::int16_t>(t.guards_near);
    auto& g = sim_->globals();
    g.textBuf = t.msg_buf;
    g.faceShown = t.popup_face;
}

void TacCaseHooks::pull(Tac& t) {
    for (std::size_t i = 0; i < t.persons.size() && i < 64; ++i) t.persons[i] = toTac(w_.persons[i]);
    auto& h = w_.hdr;
    t.known_plans = static_cast<std::uint16_t>(h.seenOrgs);
    t.guards_near = w_.cityClueCount[0];
    h.difficulty = static_cast<std::int16_t>(gameDifficulty_);
    auto& g = sim_->globals();
    t.msg_buf = g.textBuf;
    t.popup_face = g.faceShown;
    tac_ = nullptr;
}

// ---------------------------------------------------------------- the routines

void TacCaseHooks::suspectDossier(Tac& t, int who) {
    Bind b(*this, t);
    b.sim().suspectDossier(who, 0, 0);
}

void TacCaseHooks::appendPersonName(Tac& t, int p) {
    Bind b(*this, t);
    b.sim().appendPersonName(p);
}

void TacCaseHooks::personName(Tac& t, int p) {
    Bind b(*this, t);
    b.sim().personName(p);
}

int TacCaseHooks::describeNewInfo(Tac& t, int org, int city, int amount, int popup) {
    Bind b(*this, t);
    trace.push_back("describe_new_info " + std::to_string(org) + " " + std::to_string(city) + " " +
                    std::to_string(amount) + " " + std::to_string(popup));
    const int r = b.sim().describeNewInfo(org, city, amount, popup);
    trace.push_back("  -> " + std::to_string(r));
    return r;
}

int TacCaseHooks::orgAlertLevel(Tac& t, int city, int org) {
    Bind b(*this, t);
    return b.sim().orgAlertLevel(city, org);
}

int TacCaseHooks::appendBuildingType(Tac& t, int city, int org) {
    Bind b(*this, t);
    return b.sim().appendBuildingType(city, org);
}

int TacCaseHooks::setLocationKnown(Tac& t, int org, int city, int flags) {
    Bind b(*this, t);
    trace.push_back("set_location_known " + std::to_string(org) + " " + std::to_string(city) + " " +
                    std::to_string(flags));
    return b.sim().setLocationKnown(org, city, static_cast<unsigned>(flags));
}

int TacCaseHooks::loadClueText(Tac& t, int suspect, int full) {
    Bind b(*this, t);
    return b.sim().loadClueText(suspect, full);
}

void TacCaseHooks::appendNumber(Tac& t, int num) {
    Bind b(*this, t);
    b.sim().appendNumber(num);
}

int TacCaseHooks::revealRandomClue(Tac& t, int n, int org, int city) {
    Bind b(*this, t);
    trace.push_back("reveal_random_clue " + std::to_string(n));
    return b.sim().revealRandomClue(n, org, city);
}

// PERSON.C show_master_plan(org, reveal).
void TacCaseHooks::showMasterPlan(Tac& t, int org, int reveal) {
    Bind b(*this, t);
    Sim& sim = b.sim();
    auto& h = w_.hdr;
    auto& tb = sim.globals().textBuf;
    trace.push_back("show_master_plan " + std::to_string(org));
    if (org == 0) {
        h.seenOrgs = static_cast<std::int16_t>(h.seenOrgs | 1);
        ui_->pageFrame("Master Plan", 9);
        int y = 0x20;
        tb.clear();
        for (int i = 0; i < 5; ++i) tb += "--SECRET--";
        ui_->text(tb, 12, 0x10, 14);
        ui_->text(tb, 12, 0xBC, 14);
        for (int i = 0; i < sim.activeSuspects(); ++i) {
            if (sim.suspect(i).flags & 0x100) continue;
            const int x = (i & 3) * 0x48 + 0x1C;
            if (i == 4) y += 0x50;
            const int p = sim.suspect(i).person;
            if ((sim.person(p).known & 0x81) != 0x80) ui_->portrait(sim.person(p).id, x, y);
            tb.clear();
            if (sim.person(p).fate == -1) tb = "ARRESTED";
            if (sim.person(p).fate == -2) tb = "IN HIDING";
            if (sim.person(p).fate == -3) tb = " TURNED";
            ui_->text(tb, x - 1, y + 0x10, 5);
            ui_->text(tb, x - 2, y + 0x10, 12);
            tb = sim.suspect(i).name.str();
            ui_->textCentered(tb, x + 0x19, y + 0x38, 12);
            y ^= 8;
        }
    } else {
        h.seenOrgs = static_cast<std::int16_t>(static_cast<std::uint16_t>(h.seenOrgs) | (1u << (org & 15)));
        ui_->pageFrame("Personnel File", 9);
        tb = w_.orgs[static_cast<std::size_t>(org & 15)].name.str();
        tb += " Action Team";
        ui_->text(tb, 0x50, 0x10, 5);
        int y = 0x20;
        bool none = true;
        for (int i = 1; i < h.numPersons && i < 64; ++i) {
            Person& p = sim.person(i);
            if (p.org == org && (p.flags & 0x7FFF)) {
                if (reveal) p.known |= 0x0E;
                tb.clear();
                sim.appendPersonName(i);
                if (p.known & 8) {
                    tb += ", ";
                    tb += w_.cities[static_cast<std::size_t>(p.city & 15)].name.str();
                }
                ui_->text(tb, 0x50, y, 12);
                y += 8;
                none = false;
            }
        }
        if (none) ui_->text("(No activity)", 0x50, y, 12);
    }
    ui_->step(Tac::LibStep::Kind::Key);
}

// ---------------------------------------------------------------- env.sve

int TacCaseHooks::knownSite(const World& w, int org, int city) {
    for (int i = 0; i < w.hdr.numSites && i < 256; ++i) {
        const Site& s = w.sites[static_cast<std::size_t>(i)];
        if (s.org == org && s.city == city) return s.flags ? i : -1;
    }
    return -1;
}

TacSetup TacCaseHooks::setupFrom(const World& w, int program, int env12, std::uint32_t runSeed) {
    const auto& h = w.hdr;
    TacSetup s;
    s.env0 = program;
    s.practice = false;
    s.female = h.options & 2;
    s.sound = h.options & 4;
    s.difficulty = h.difficulty;
    s.skillLevels = h.skillLevels;
    s.alertLevel = h.var48;
    s.org = h.hereOrg;
    s.city = h.hereCity;
    s.gameSeed = static_cast<std::uint16_t>(h.randSeed);
    s.clockBase = static_cast<std::uint16_t>(h.clockBase);
    s.raidedCities = static_cast<std::uint16_t>(h.doubleAgents);
    s.knownPlans = static_cast<std::uint16_t>(h.seenOrgs);
    s.nClues = h.numSuspects;
    s.cityClueCount0 = w.cityClueCount[0];
    for (const Person& p : w.persons) s.persons.push_back(toTac(p));
    s.nPersons = h.numPersons;
    s.rescueTarget = static_cast<std::int8_t>(env12);
    for (std::size_t i = 0; i < 4; ++i) {
        s.agents[i].name = w.items[i].name.str();
        s.agents[i].rank = w.items[i].icon;
        s.agents[i].person = w.items[i].holder;
    }
    for (std::size_t i = 0; i < 16; ++i) {
        s.cityNames[i] = w.cities[i].name.str();
        s.orgNames[i] = w.orgs[i].name.str();
    }
    if (const int site = knownSite(w, h.hereOrg, h.hereCity); site >= 0) {
        const Site& st = w.sites[static_cast<std::size_t>(site)];
        s.roomsSeen = static_cast<std::uint16_t>(st.f8) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(st.fa)) << 16;
    }
    s.runSeed = runSeed;
    return s;
}

void TacCaseHooks::applyResult(World& w, const TacResult& r) {
    auto& h = w.hdr;
    h.skillLevels = static_cast<std::int16_t>((h.skillLevels & ~0x300) | (r.gameSpeed & 3) << 8);
    for (std::size_t i = 0; i < r.persons.size() && i < 64; ++i) w.persons[i] = toWorld(r.persons[i]);
    h.seenOrgs = static_cast<std::int16_t>(r.knownPlans);
    w.cityClueCount[0] = static_cast<std::int16_t>(r.cityClueCount0);
    if (const int site = knownSite(w, h.hereOrg, h.hereCity); site >= 0) {
        Site& st = w.sites[static_cast<std::size_t>(site)];
        st.f8 = static_cast<std::int16_t>(static_cast<std::uint16_t>(st.f8) | (r.roomsSeen & 0xFFFF));
        st.fa = static_cast<std::int16_t>(static_cast<std::uint16_t>(st.fa) | (r.roomsSeen >> 16));
    }
}

}  // namespace redux
