// The library's screens recorded as steps. See LibScreen.h.
#include "game/LibScreen.h"

#include <utility>

#include "formats/TextWindow.h"
#include "game/Paper.h"

namespace redux {

namespace {

std::string oneLine(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '\n' ? '|' : c;
    return o;
}

std::uint8_t u8(int c) { return static_cast<std::uint8_t>(c); }

}  // namespace

int LibRecorder::event(const SimEvent& e) {
    using K = SimEvent::Kind;
    switch (e.kind) {
    case K::Notice:
        log("notice " + oneLine(e.text));
        step(LibStep::Kind::Menu, e.text, e.x, e.y);
        break;
    case K::Wait: step(LibStep::Kind::Key); break;
    case K::Popup:
        log("popup " + std::to_string(e.index) + " " + oneLine(e.text));
        if (popup) {
            popup(e);
        } else {
            step(LibStep::Kind::Popup, e.text, 0, 0, e.index);
            if (e.index > -1 && sim) sim->globals().faceShown = -1;   // (the portrait's draw_face_parts)
        }
        break;
    case K::Dossier: log("dossier " + std::to_string(e.index)); break;
    case K::Clue:
        log(std::string(e.title.empty() ? "clue " : "  related ") + std::to_string(e.index) + " " + oneLine(e.text));
        break;
    case K::NewInfo: log("newinfo " + std::to_string(e.index) + " " + oneLine(e.text)); break;
    default: break;
    }
    return -1;
}

void LibRecorder::pageFrame(const std::string& title, int color) {
    draw([title, color](LibCanvas& c) {
        drawPageFrame(c.img, c.font, c.faces, title, u8(color));
        c.fillcolor = 15;
    });
}

void LibRecorder::text(const std::string& s, int x, int y, int col) {
    draw([s, x, y, col](LibCanvas& c) { c.font.draw(c.img, x, y, s, {0, u8(col), 0}); });
}

void LibRecorder::textOpaque(const std::string& s, int x, int y, int col) {
    draw([s, x, y, col](LibCanvas& c) {
        TextAttr a;
        a.textbg = 1;
        a.color = u8(col);
        a.background = u8(c.fillcolor);
        c.font.draw(c.img, x, y, s, a);
    });
}

void LibRecorder::textCentered(const std::string& s, int x, int y, int col) {
    draw([s, x, y, col](LibCanvas& c) { drawTextCentered(c.img, c.font, s, x, y, u8(col)); });
}

void LibRecorder::line(int x1, int y1, int x2, int y2, int col) {
    draw([=](LibCanvas& c) { drawLine(c.img, x1, y1, x2, y2, u8(col)); });
}

void LibRecorder::wrapped(const std::string& s, int width, int x, int y, int col) {
    draw([s, width, x, y, col](LibCanvas& c) { drawWrappedText(c.img, c.font, s, width, x, y, u8(col)); });
}

void LibRecorder::portrait(int face, int x, int y) {
    draw([=](LibCanvas& c) { drawPortrait(c.img, c.faces, face, x, y); });
    if (sim) sim->globals().faceShown = -1;   // draw_face_parts
}

void LibRecorder::sprite(int n, int x, int y) {
    draw([=](LibCanvas& c) { c.faces.draw(c.img, n, x, y); });
}

void LibRecorder::cardPanel(int x, int y) {
    draw([=](LibCanvas& c) { drawCardPanel(c.img, x, y); });
}

void LibRecorder::bevelBox(int x, int y, int w, int h, int col) {
    draw([=](LibCanvas& c) {
        drawBevelBox(c.img, x, y, w, h, u8(col));
        c.fillcolor = col;
    });
}

void LibRecorder::fillRect(int x, int y, int w, int h, int col) {
    draw([=](LibCanvas& c) { redux::fillRect(c.img, x, y, w, h, u8(col)); });
}

void LibRecorder::step(LibStep::Kind k, std::string t, int x, int y, int n) {
    if (!out) return;
    LibStep s;
    s.kind = k;
    s.text = std::move(t);
    s.x = x;
    s.y = y;
    s.n = n;
    out->push_back(std::move(s));
}

void LibRecorder::draw(std::function<void(LibCanvas&)> f) {
    if (!out) return;
    LibStep s;
    s.draw = std::move(f);
    out->push_back(std::move(s));
}

void libShowMasterPlan(Sim& sim, LibRecorder& ui, int org, int reveal) {
    World& w = sim.world();
    auto& h = w.hdr;
    auto& tb = sim.globals().textBuf;
    ui.log("show_master_plan " + std::to_string(org));
    if (org == 0) {
        h.seenOrgs = static_cast<std::int16_t>(h.seenOrgs | 1);
        ui.pageFrame("Master Plan", 9);
        int y = 0x20;
        tb.clear();
        for (int i = 0; i < 5; ++i) tb += "--SECRET--";
        ui.text(tb, 12, 0x10, 14);
        ui.text(tb, 12, 0xBC, 14);
        for (int i = 0; i < sim.activeSuspects(); ++i) {
            if (sim.suspect(i).flags & 0x100) continue;
            const int x = (i & 3) * 0x48 + 0x1C;
            if (i == 4) y += 0x50;
            const int p = sim.suspect(i).person;
            if ((sim.person(p).known & 0x81) != 0x80) ui.portrait(sim.person(p).id, x, y);
            tb.clear();
            if (sim.person(p).fate == -1) tb = "ARRESTED";
            if (sim.person(p).fate == -2) tb = "IN HIDING";
            if (sim.person(p).fate == -3) tb = " TURNED";
            ui.text(tb, x - 1, y + 0x10, 5);
            ui.text(tb, x - 2, y + 0x10, 12);
            tb = sim.suspect(i).name.str();
            ui.textCentered(tb, x + 0x19, y + 0x38, 12);
            y ^= 8;
        }
    } else {
        h.seenOrgs = static_cast<std::int16_t>(static_cast<std::uint16_t>(h.seenOrgs) | (1u << (org & 15)));
        ui.pageFrame("Personnel File", 9);
        tb = w.orgs[static_cast<std::size_t>(org & 15)].name.str();
        tb += " Action Team";
        ui.text(tb, 0x50, 0x10, 5);
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
                    tb += w.cities[static_cast<std::size_t>(p.city & 15)].name.str();
                }
                ui.text(tb, 0x50, y, 12);
                y += 8;
                none = false;
            }
        }
        if (none) ui.text("(No activity)", 0x50, y, 12);
    }
    ui.step(LibStep::Kind::Key);
}

}  // namespace redux
