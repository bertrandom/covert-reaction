// GAME.C main and set_exit_state, CITY.C (city_loop, building_screen,
// watch_building, success_briefing and the returns from the mini-games) and
// TRAVEL.C (travel_screen, draw_city_label, fly_to_city). See CityGame.h.
#include "app/CityGame.h"

#include <cstdlib>

namespace redux {

namespace {

std::uint16_t bit16(int n) {
    n &= 31;
    return n < 16 ? static_cast<std::uint16_t>(1u << n) : 0;
}

int sign(int n) { return n > 0 ? 1 : n < 0 ? -1 : 0; }

constexpr int kHeadingDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kHeadingDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kHeadings[3][3] = {{7, 6, 5}, {0, 0, 4}, {1, 2, 3}};

}  // namespace

CityGame::CityGame(App& app, CityEnv& env, CityUi& ui, std::uint32_t seed)
    : app_(app), env_(env), ui_(ui), sim_(app.game.world, app.game.rng, app.data, app.game.career) {
    app_.game.rng.seed(seed);   // randomize()
    sim_.setUi(&ui_);
    ui_.setSim(&sim_);
    ui_.animations = (h().options & 8) != 0 || env_[1] == 0;
}

// ---------------------------------------------------------------- GAME.C

int CityGame::run() {
    ui_.clearScreenNow();
    if (env_[1] == 0) {   // a new game
        sim_.startNewGame();   // jailbreak_person = -1, mission_briefing, here_org = 1, options |= 0Ch
        env_[1] = 1;
        lastExit_ = 99;
    } else {
        h().difficulty = h().var50;   // difficulty = ENV+50h
        lastExit_ = env_.signedAt(7);
        const bool advance = (lastExit_ != 3 || env_[9] != 2) && (lastExit_ != 1 || !(env_[10] & 2));
        if (advance) sim_.resumeGame(env_[15]);
        else sim_.drawStatusBar();
    }
    ui_.animations = (h().options & 8) != 0;
    env_[0] = 0xFF;
    ui_.flushInput();
    do {
        cityLoop(h().hereCity);
        if (!cityExit_) travelScreen();
    } while (!cityExit_);
    ui_.showMenu("One moment please...\n", 100, 2, SimEvent::kNoWait);
    setExitState();
    if (env_[0] == 0xFF) {   // quit: the advertisement, then DOS
        ui_.loadPicture("ad.pic", 0);
        ui_.waitKey();
    }
    return cityExit_;
}

void CityGame::setExitState() {
    sim_.setExitState();   // ENV+48h, ENV+50h
    env_[7] = env_[0] = static_cast<std::uint8_t>(cityExit_);
    env_[15] = 0;
}

// ---------------------------------------------------------------- CITY.C

void CityGame::cityLoop(int city) {
    int choice = 0;
    do {
        ui_.statusY = 0xAE;
        if (lastExit_ == 0 && h().jailbreakPerson != -1) {
            const int jb = h().jailbreakPerson;
            ui_.setSceneBackground("warning.pan", 0xA8, 0);
            tb() = "JailBreak in ";
            tb() += cityName(person(jb).city);
            tb() += "!\n";
            tb() += orgName(person(jb).org);
            tb() += " agents\nare trying to\nfree ";
            sim_.appendPersonName(jb);
            tb() += "!\n";
            ui_.showMenu(tb(), 16, 21);
            person(jb).known |= 0x40;
            env_[12] = static_cast<std::uint8_t>(jb);
            h().jailbreakPerson = -1;
            cityExit_ = 8;
            h().hereOrg = 1;
            return;
        }
        if (lastExit_ == 8) {
            const int whos = env_.signedAt(12);
            if (env_[8] == 0xFF) {
                ui_.setSceneBackground("bustout.pan", 0xA8, 0);
                ui_.setSceneVar(0, (person(whos).id & 1) ? -1 : 1);
                sim_.appendPersonName(whos);
                tb() += " escapes!\n";
                person(whos).fate = 99;
                person(whos).known |= 0x40;
                sim_.clearLocation(person(whos).org, person(whos).city);
                sim_.addChronologyEntry(10, whos);
            } else {
                ui_.clearPlayAreaNow();
                tb() += "Escape attempt foiled!\n";
            }
            ui_.showMenu(tb(), 20, 21);
            lastExit_ = 0;
        }
        if (lastExit_ == 12) {
            sim_.raiseAlertness(h().hereOrg, h().hereCity, env_.signedAt(9) * 25);
            if (env_.signedAt(8) >= 25) {
                sim_.setLocationKnown(h().hereOrg, city, 8);
                tb() = "Your car tracer leads\nyou to a new building.\n";
                ui_.showMenu(tb(), 16, 120);
            } else {
                lastExit_ = 0;
            }
        }
        if (lastExit_ == 3) {
            if (env_[9] == 1) sim_.setLocationKnown(h().hereOrg, city, 8);
            else lastExit_ = 0;
            if (env_[9] == 2) sim_.interrogateSuspect(env_.signedAt(8), env_[13] == 0);
            sim_.raiseAlertness(h().hereOrg, city, env_.signedAt(10));
        }
        if (lastExit_ == 6) {
            if (env_[9]) {
                ui_.clearScreenNow();
                tb().clear();
                sim_.showProse("*carcap", 32);
                ui_.showMenu(tb(), 20, 32);
                env_[12] = static_cast<std::uint8_t>(h().hereOrg);
                cityExit_ = 10;
                return;
            }
            h().hereOrg = static_cast<std::int16_t>(env_.signedAt(14));
            sim_.heat() = 0;
            lastExit_ = 3;
        }
        if (lastExit_ == 7) {
            if (env_[8] == 0xFF) {
                const int r = sim_.capturedScreen(false);
                if (r == 0) leftBuilding_ = 1;
                else cityExit_ = r;
            }
            if (cityExit_) return;
            lastExit_ = 0;
        }
        if (((!(env_[10] & 2) && env_[8] != 0xFF && lastExit_ == 1) || lastExit_ == 7 ||
             (lastExit_ == 10 && env_[8] != 0xFF)) &&
            env_[11]) {
            ui_.setSceneBackground("wounded.pan", 0xE0, 0);
            tb() = "You take time\nto recover from\nyour wounds\n";
            ui_.showMenu(tb(), 20, 21);
            for (int i = 0; i < env_.signedAt(11); ++i)
                for (int j = 0; j < 6; ++j) advanceTime(60);
            if (lastExit_ != 10) lastExit_ = 0;
        }
        if (lastExit_ == 10) {
            const int orgd = env_.signedAt(12);
            sim_.heat() = 0;
            const int whos = sim_.pickClue(orgd, city, 0, 0);
            if (whos != -1 && person(whos).fate > 0) person(whos).fate = 0;
            if (env_[8] == 0xFF) {
                if (!difficulty()) {
                    tb().clear();
                    sim_.showProse("*surpriseL", 32, orgName(orgd));
                    ui_.showMenu(tb(), 20, 32);
                } else {
                    h().hereOrg = static_cast<std::int16_t>(orgd);
                    const int r = sim_.capturedScreen(false);
                    if (r == 0) leftBuilding_ = 1;
                    else cityExit_ = r;
                    if (cityExit_) return;
                }
            } else {
                tb().clear();
                sim_.showProse("*surpriseW", 32, orgName(orgd));
                ui_.showMenu(tb(), 20, 32);
                sim_.setLocationKnown(orgd, h().hereCity, 12);
            }
            lastExit_ = 0;
        }
        for (;;) {
            ui_.clearScreenNow();
            if (lastExit_ == 0) ui_.loadCityPicture(h().hereCity, 240, 140);
            ui_.fillColor = 5;
            advanceTime(0);
            auto& t = tb();
            t = "You are in ";
            t += cityName(h().hereCity);
            t += "\nDo you go to ...\n";
            t += " The Airport.\n";
            t += " Your Hotel.\n ";
            menuCount_ = 2;
            for (int i = 1; i < 16; ++i) {
                const int sitev = sim_.setLocationKnown(i, city, 0);
                if (sitev != -1) {
                    menuItems_[static_cast<std::size_t>(menuCount_++)] = i;
                    if (w().sites[static_cast<std::size_t>(sitev)].flags & 4) {
                        t += orgName(i);
                        sim_.appendBuildingType(city, i);
                    } else {
                        t += "unknown building";
                    }
                    t += "\n ";
                }
            }
            for (int i = 0; i < 80; ++i) {
                const Clue& c = w().clues[static_cast<std::size_t>(i)];
                if (c.suspect == -1) continue;
                const int pp = sim_.suspect(c.suspect).person;
                if ((c.text >> 8) == 2 && (c.kind >> 8) == 8 && person(pp).city == h().hereCity) {
                    sim_.appendNumber(c.text);
                    t += "\n ";
                    menuItems_[static_cast<std::size_t>(menuCount_++)] = person(pp).org;
                }
            }
            t += "Check Data\n";
            menuItems_[static_cast<std::size_t>(menuCount_)] = -1;
            if (lastExit_ == 0) {
                choice = ui_.showMenu(tb(), 16, 9);
                const int whos = sim_.cityAmbushCheck(choice, menuCount_);
                if (whos >= 0) {
                    env_[12] = static_cast<std::uint8_t>(whos);
                    cityExit_ = 10;
                    return;
                }
            } else {
                switch (lastExit_) {
                case 1: case 3: case 4: case 12: case 99:
                    choice = 2;
                    menuItems_[2] = h().hereOrg;
                    break;
                case 2:
                    choice = 2;
                    menuItems_[2] = 1;
                    break;
                default:
                    break;
                }
            }
            if (choice == menuCount_) dataFilesMenu();
            else break;
        }
        if (choice == 1) hotelScreen();
        else if (lastExit_ != 1 || !(env_[10] & 2)) advanceTime(120);
        if (choice > 1 && (h().hereOrg = static_cast<std::int16_t>(menuItems_[static_cast<std::size_t>(choice)])) != 0)
            buildingScreen();
    } while (choice > 0 && !cityExit_);
}

void CityGame::buildingScreen() {
    leftBuilding_ = 0;
    do {
        if (lastExit_ == 3 || lastExit_ == 12 || lastExit_ == 99) lastExit_ = 0;
        const int cityb = h().hereCity;
        const int orgg = h().hereOrg;
        int choice = -1;
        int lvl = 0;
        if (orgg == 1) {
            choice = -2;
        } else {
            auto& t = tb();
            t = "You are at the\n";
            const int site = sim_.setLocationKnown(orgg, cityb, 0);
            if (site >= 0 && (w().sites[static_cast<std::size_t>(site)].flags & 4)) {
                t += orgName(orgg);
                btypek_ = sim_.appendBuildingType(cityb, orgg);
            } else {
                t += "unknown building";
            }
            const std::size_t len = t.size();
            lvl = sim_.describeAlertLevel(orgg, cityb);
            t.resize(len);
            switch (lvl / 16) {
            case 0: break;
            case 1: t += "\nA few guards are\npeering through the\nwindows."; break;
            case 2: t += "\nIt appears extra\nguards have been\nposted."; break;
            default: t += "\nThe building is\nswarming with guards."; break;
            }
            t += "\nDo you ...\n Place wiretap\n";
            t += " Break into building\n";
            t += " Watch the building\n Check Data\n Leave\n";
            ui_.fillColor = 5;
            if (lastExit_ == 0) {
                std::string scene = "blding01.pan";
                btypek_ = clampTo(btypek_ - 2, 0, 3);
                scene[7] = static_cast<char>(btypek_ + '1');
                ui_.statusY = 4;
                ui_.setSceneBackground(scene, 0, 0);
                ui_.setSceneVar(10, clampTo(lvl / 16 + 1, 1, 4));
                choice = ui_.showMenu(tb(), 2, 33);
            } else {
                switch (lastExit_) {
                case 1: choice = 1; break;
                case 3: case 4: choice = 0; break;
                default: break;
                }
            }
        }
        switch (choice) {
        case -2:
            sim_.ciaContactScreen(orgg, cityb, lastExit_ == 0);
            if (h().hereOrg == 1) ciaHqScreen();
            lastExit_ = 0;
            leftBuilding_ = 1;
            break;
        case 2:
            watchBuilding(orgg, cityb);
            if (h().hereOrg == 1) leftBuilding_ = 1;
            break;
        case 1:
            if (lastExit_ == 0 && firstVisit_) {
                ui_.statusY = 4;
                advanceTime(60);
                cityExit_ = 1;
                ui_.loadPicture("sneakin.pic", 0);   // (on the back page, then shown: drv_slot9)
                ui_.wait(60);
            } else {
                successBriefing(orgg, cityb);
                lastExit_ = 0;
            }
            break;
        case 0:
            if (lastExit_ == 0 && firstVisit_) {
                ui_.loadPicture("wiretap.pic", 0);
                tb() = "The guards are ";
                lvl = sim_.describeAlertLevel(orgg, cityb);
                lvl = clampTo(10 - lvl / 5, 2, 10);
                tb() += ".\nYou will have ";
                tb() += std::to_string(lvl);
                tb() += " minutes.\n";
                ui_.showMenu(tb(), 20, 20, SimEvent::kNoWait);
                cityExit_ = 4;
            } else {
                lastExit_ = 0;
                sim_.wiretapResult(orgg, cityb, env_.signedAt(8), env_.signedAt(9), firstVisit_ != 0, scorex_);
            }
            break;
        case 3:
            dataFilesMenu();
            break;
        case -1:
        case 4:
            leftBuilding_ = 1;
            if (difficulty() && sim_.describeAlertLevel(h().hereOrg, h().hereCity) > 50) {
                ui_.setSceneBackground("followed.pan", 0, 0);
                tb().clear();
                sim_.showProse("*followed", 32);
                ui_.showMenu(tb(), 24, 0x91);
                env_[9] = static_cast<std::uint8_t>(h().hereOrg);
                cityExit_ = 6;
            }
            break;
        default:
            break;
        }
    } while (!leftBuilding_ && !cityExit_);
    if (h().hereOrg == 1 && (sim_.moleCities() & bit16(h().hereCity))) sim_.raiseAlertness(0, h().hereCity, 40);
}

void CityGame::watchBuilding(int org, int city) {
    for (;;) {
        ui_.fill(0, 0, 320, 200, 5);
        ui_.portraitPos(26, 45);
        const Sim::WatchResult r = sim_.watchBuilding(org, city);
        const int who = r.who;
        switch (r.choice) {
        case -1:
            return;
        case 0:
        case 3:
            sim_.watchBuildingWait(who);
            if (r.choice == 3) dataFilesMenu();
            break;
        case 1:
            tb() = (person(who).id & 1) ? "He looks " : "She looks ";
            sim_.describeAlertLevel(person(who).org, person(who).city);
            tb() += "\n.";
            ui_.showMenu(tb(), 20, 100, SimEvent::kNoWait);
            env_[8] = static_cast<std::uint8_t>(who);
            env_[9] = static_cast<std::uint8_t>(h().hereOrg);
            env_[10] = static_cast<std::uint8_t>(person(who).id & 1);
            cityExit_ = 3;
            h().hereOrg = static_cast<std::int16_t>(r.bestOrg);
            return;
        case 2:
            ui_.playAnimation("bugcar.pan", 0);
            cityExit_ = 12;
            h().hereOrg = static_cast<std::int16_t>(r.bestOrg);
            return;
        default:
            return;
        }
    }
}

void CityGame::successBriefing(int org, int city) {
    Sim::MiniGameResult res;
    res.lastExit = lastExit_;
    res.env8 = env_.signedAt(8);
    res.env9 = env_.signedAt(9);
    res.env10 = env_[10];
    res.env13 = env_[13];
    res.scorexIn = scorex_;
    const int r = sim_.successBriefing(org, city, res);
    if (res.lastExit && res.env8 == -1) {   // captured: escape (city_exit 7) or exchanged (left the building)
        if (r) cityExit_ = r;
        else leftBuilding_ = 1;
    }
}

// ---------------------------------------------------------------- TRAVEL.C

void CityGame::travelScreen() {
    for (;;) {
        switch (w().cities[0].name.raw[2]) {
        case 's': ui_.loadPicture("europe.pic", 1); break;
        case 'm': ui_.loadPicture("africa.pic", 1); break;
        case 'g': ui_.loadPicture("central.pic", 1); break;
        default: break;
        }
        ui_.screen = ui_.back;
        auto& t = tb();
        t = "You're in ";
        t += cityName(h().hereCity);
        t += "\nDo you travel to ...\n";
        t += " Stay here\n";
        for (int i = 0; i < 16; ++i) {
            t += " ";
            t += cityName(i);
            t += "\n";
        }
        t += " Check Data\n";
        ui_.fillColor = 0;
        ui_.drawWrapped(t, 0x28, 0xC2, 9, 7);
        ui_.recolor(0xC0, 9, 0x80, 0x10, 7, 15);
        ui_.back = ui_.screen;
        int cursor = -1, n = -1;
        bool done = false;
        do {
            drawCityLabel(h().hereCity, 7);
            if (cursor >= 0 && cursor < 16) drawCityLabel(cursor, 15);
            ui_.recolor(0xC0, cursor * 8 + 0x21, 0x80, 8, 7, 11);
            n = cursor;
            const int key = ui_.getKey();
            switch (key) {
            case 0x5000: ++cursor; break;
            case 0x4800: --cursor; break;
            case 0x1B:
                cursor = -1;
                done = true;
                break;
            case 0x0D:
            case ' ':
                done = true;
                break;
            default:
                break;
            }
            cursor = clampTo(cursor, -1, 16);
            if (!done) {
                ui_.screen.blit(ui_.back, 0, 0, 0xC0, 200, 0, 0);
                ui_.recolor(0xC0, n * 8 + 0x21, 0x80, 8, 11, 7);
            }
        } while (!done);
        ui_.flushInput();
        ui_.fillColor = 7;
        if (cursor == h().hereCity) cursor = -1;
        n = cursor + 1;
        if (n != 0 && n < 17) {
            const int city = n - 1;
            flyToCity(h().hereCity, city);   // (the drawing; Sim::travelTo does its random(8) and the rest)
            sim_.travelTo(city);
            return;
        }
        if (n != 17) return;
        dataFilesMenu();
    }
}

void CityGame::drawCityLabel(int c, int color) {
    const City& cc = w().cities[static_cast<std::size_t>(c & 15)];
    ui_.fill(cc.mapX - 1, cc.mapY - 1, 3, 3, 3);
    const std::string name = cc.name.str();
    const int width = ui_.font().textWidth(name);
    ui_.fill(cc.mapX - 1, cc.mapY - 1, 3, 3, 11);
    if (static_cast<unsigned>(cc.mapX) < static_cast<unsigned>(0xBD - width))
        ui_.drawTextT(name, cc.mapX + 3, cc.mapY - 3, color);
    else
        ui_.drawTextT(name, cc.mapX - width - 3, cc.mapY - 3, color);
}

// fly_to_city's drawing: the plane and its shadow across the map, a frame every
// 10 retraces (the random(8) it draws first doesn't change the path).
void CityGame::flyToCity(int from, int to) {
    ui_.back.blit(ui_.screen, 0, 0, 0xC0, 200, 0, 0);
    const City& a = w().cities[static_cast<std::size_t>(from & 15)];
    const City& b = w().cities[static_cast<std::size_t>(to & 15)];
    int x = a.mapX, y = a.mapY;
    const int tx = b.mapX, ty = b.mapY;
    int dx = tx - x, dy = ty - y;
    int dir = kHeadings[sign(dx) + 1][sign(dy) + 1];
    int alt = 0, frames = 0;
    long guard = 0;
    app_.sound(0x23);   // the plane takes off
    int lastAlt = 0;
    do {
        if (const int ddir = kHeadings[sign(dx) + 1][sign(dy) + 1] - dir; ddir != 0) {
            if ((ddir & 7) < 4) ++dir;
            else --dir;
            dir &= 7;
        }
        alt = clampTo((frames++ & 1) + alt, 0, (std::abs(dx) + std::abs(dy)) / 2);
        if (alt > 8) alt = 8;
        if (alt < lastAlt) {   // ...and comes down: the landing
            app_.sound(0x24);
            lastAlt = -1;
        }
        if (lastAlt != -1) lastAlt = alt;
        x += kHeadingDx[dir];
        y += kHeadingDy[dir];
        const int sx = clampTo(x - 5, 0, 999);
        ui_.screen.blit(ui_.back, sx, y - alt - 6, 10, alt + 12, sx, y - alt - 6);
        ui_.drawSprite(dir + 0x58, sx + 1, y - 4);
        ui_.drawSprite(dir + 0x50, sx + 1, y - alt - 4);
        ui_.wait(10);
        dx = tx - x;
        dy = ty - y;
    } while ((dx || dy) && ++guard < 100000);
}

}  // namespace redux
