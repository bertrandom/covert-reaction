// SCREENS.C (the hotel, the CIA building, the data files), SUSPECTS.C
// suspects_overview, CHRONO.C chronology_screen and PERSON.C show_master_plan.
// See CityGame.h.
#include "app/CityGame.h"
#include "app/LoadGame.h"
#include "game/Paper.h"
#include "game/SaveGame.h"

namespace reaction {

namespace {

std::uint16_t bit16(int n) {
    n &= 31;
    return n < 16 ? static_cast<std::uint16_t>(1u << n) : 0;
}

constexpr int kTabColors[4] = {14, 10, 14, 10};

}  // namespace

// ---------------------------------------------------------------- the hotel

void CityGame::hotelRest() {
    // hotel_rest: an hour at a time until a key (or the end of the case).
    while (!cityExit_) {
        if (ui_.keyPending()) break;
        sim_.hotelRest(1);
    }
}

void CityGame::hotelScreen() {
    do {
        ui_.loadPicture("hotel.pic", 0);
        tb() = "You are at your\nhotel. Do you...\n Leave Hotel\n Visit the lounge\n Sleep through case\n Save Game\n Load "
               "Game\n \n Quit\n";
        switch (ui_.showMenu(tb(), 0x10, 0x28)) {
        case -1:
        case 0:
            return;
        case 2:
            hotelRest();
            break;
        case 1:
            sim_.hotelLounge();
            break;
        case 3: {
            savegame::beforeCitySave(app_.game);   // options &= ~40h, ENV+50h = difficulty
            const Frame f = ui_.runScene([this](const Frame& last, auto then) {
                return makeSaveGameScene(app_, saveSlot_, last, std::move(then));
            });
            ui_.screen = f.image;
            break;
        }
        case 4: {
            const int oldOptions = h().options;
            const Frame f = ui_.runScene([this](const Frame& last, auto then) {
                return makeLoadGameScene(app_, last, [then](bool, const Frame& fr) { return then(fr); });
            });
            ui_.screen = f.image;
            h().options = static_cast<std::int16_t>((oldOptions & 0x10) | (h().options & ~0x10));
            advanceTime(0);
            break;
        }
        case 6:
            cityExit_ = -1;
            break;
        default:
            break;
        }
    } while (!cityExit_);
}

// ---------------------------------------------------------------- data files

void CityGame::dataFilesMenu() {
    int choice;
    do {
        ui_.clearScreenNow();
        ui_.drawSprite(0x65, 0x40, 4);
        tb() = "Data Files...\n Clues\n Suspects\n Inside Information\n News Bulletins\n";
        tb() += " Organization Summaries\n City Summaries\n Activity Reports\n";
        choice = ui_.showMenu(tb(), 0x14, 0x1F);
        ui_.clearScreenNow();
        switch (choice) {
        case 0: suspectsOverview(); break;
        case 1: suspectFilesScreen(); break;
        case 2: documentsScreen(); break;
        case 3: chronologyScreen(false); break;
        case 4: organizationSummary(); break;
        case 5: citySummary(); break;
        case 6: activityReport(); break;
        default: break;
        }
    } while (choice != -1);
}

void CityGame::ciaHqScreen() {
    if (lastExit_ == 2) {
        if (env_[9]) {
            codedMessagesScreen();
            sim_.showDecodedMessage(env_.signedAt(8));
            ui_.screenW = 320;
        }
        advanceTime(env_.signedAt(10) * 30);
        lastExit_ = 0;
    }
    for (;;) {
        ui_.clearScreenNow();
        tb() = "You are in the CIA building.\nWhich floor ?\n 1. Data Section\n 2. Intelligence Section\n 3. Crypto "
               "Branch\n Leave building\n";
        int sub = 0;
        switch (ui_.showMenu(tb(), 8, 0x15)) {
        case -1:
            return;
        case 1:
            do {
                ui_.setSceneBackground("comm.pan", 0xA8, 0);
                tb() = "Intelligence Section...\n Local Scan\n International Scan\n Active Wire Taps\n Accuse Double "
                       "Agent\n Check with Sam\n";
                switch (sub = ui_.showMenu(tb(), 0x14, 0x15)) {
                case 0:
                    sim_.localScan();
                    break;
                case 1:
                    sim_.internationalScan();
                    break;
                case 2:
                    wiretapsScreen();
                    break;
                case 3:
                    if (!sim_.accuseDoubleAgent()) return;
                    break;
                case 4:
                    sim_.researchOffice();
                    break;
                default:
                    break;
                }
            } while (sub != -1);
            break;
        case 0:
            do {
                ui_.setSceneBackground("research.pan", 0xA8, 0);
                tb() = "Data Section...\n Review Clues\n Review Suspects\n Inside Information\n News Bulletins\n";
                tb() += " Organization Summary\n City Summary\n Activity Reports\n";
                switch (sub = ui_.showMenu(tb(), 0x14, 0x15)) {
                case 0: suspectsOverview(); break;
                case 1: suspectFilesScreen(); break;
                case 2: documentsScreen(); break;
                case 3: chronologyScreen(false); break;
                case 4: organizationSummary(); break;
                case 5: citySummary(); break;
                case 6: activityReport(); break;
                default: break;
                }
            } while (sub != -1);
            break;
        case 2:
            do {
                ui_.setSceneBackground("crypto.pan", 0xA8, 0);
                tb() = "Crypto Branch...\n Coded Messages\n Crime Chronology\n";
                switch (sub = ui_.showMenu(tb(), 0x14, 0x15)) {
                case 0:
                    if (!lastExit_) {
                        env_[8] = 0xFF;
                        codedMessagesScreen();
                        if (env_[8] != 0xFF) cityExit_ = 2;
                    }
                    break;
                case 1:
                    chronologyScreen(true);
                    break;
                default:
                    break;
                }
            } while (sub != -1 && !cityExit_);
            break;
        case 3:
            return;
        default:
            break;
        }
        if (!cityExit_) continue;
        return;
    }
}

void CityGame::activityReport() {
    ui_.drawBevel(0, 0, 0x13F, 0xC7, 1);
    int csum = 0, osum = 0;
    for (int i = 0; i < 16; ++i) {
        csum += sim_.cityClueCount(i);
        osum += sim_.orgClueCount(i);
    }
    ui_.drawText("Activity Report Summary", 0x60, 6, 0xF);
    for (int y = 0x14, i = 0; i < 16; y += 8, ++i) {
        ui_.drawText(cityName(i), 2, y, 3);
        // (a division by zero in the original when nothing has been reported)
        if (csum) ui_.fill(0x52, y + 1, 0xA0 * sim_.cityClueCount(i) / csum, 6, 0xB);
        if (i) {
            ui_.drawText(orgAbbrev(i), 0xA2, y - 8, 0xC);
            if (osum) ui_.fill(0xF2, y - 7, 0xA0 * sim_.orgClueCount(i) / osum, 6, 0xE);
        }
    }
    ui_.waitKey();
}

void CityGame::wiretapsScreen() {
    ui_.drawPageFrame("WireTaps", 7);
    tb().clear();
    bool found = false;
    for (int i = 0; i < h().numSites && i < 256; ++i) {
        const Site& s = w().sites[static_cast<std::size_t>(i)];
        if (s.strength > 1) {
            tb() += orgName(s.org);
            tb() += "/";
            tb() += cityName(s.city);
            tb() += " ... ";
            tb() += std::to_string(s.strength);
            tb() += "%\n";
            found = true;
        }
    }
    if (!found) tb() += "none \n";
    ui_.showMenu(tb(), 0x32, 0x15);
}

void CityGame::organizationSummary() {
    for (;;) {
        tb() = "Which organization ...\n";
        menuCount_ = 0;
        for (int i = 1; i < 16; ++i) {   // append_org_name
            if (w().orgs[static_cast<std::size_t>(i)].flags & 1) {
                tb() += " ";
                tb() += orgName(i);
                tb() += "\n";
                menuItems_[static_cast<std::size_t>(menuCount_++)] = i;
            }
        }
        ui_.clearPlayAreaNow();
        int o = ui_.showMenu(tb(), 0x10, 0x15);
        if (o == -1) break;
        o = menuItems_[static_cast<std::size_t>(o)];
        ui_.drawPageFrame(orgName(o), 0xC);
        ui_.drawTextCentered(orgName(o), 0xA0, 0x10, 5);
        tb() = "Allies: \n ";
        for (int i = 1; i < 16; ++i) {
            if (Sim::cityDistance(static_cast<std::uint16_t>(w().orgs[static_cast<std::size_t>(o)].home),
                                  static_cast<std::uint16_t>(w().orgs[static_cast<std::size_t>(i)].home)) < 8 &&
                i != o) {
                tb() += orgName(i);
                tb() += "\n ";
            }
        }
        tb() += "none";   // (empty is never cleared)
        ui_.showMenu(tb(), 0x14, 0x17, SimEvent::kNoWait);
        tb() = "Known Locations:\n-";
        for (int i = 0; i < 16; ++i) {
            if (sim_.orgAlertLevel(i, o) && (w().cities[static_cast<std::size_t>(i)].orgsKnown & bit16(o))) {
                const int si = sim_.setLocationKnown(o, i, 0);
                if (si >= 0 && (w().sites[static_cast<std::size_t>(si)].flags & 4)) {
                    sim_.appendBuildingType(i, o);
                    tb() += " in ";
                    tb() += cityName(i);
                    tb() += "\n-";
                }
            }
        }
        ui_.showMenu(tb(), 0x90, 0x17, SimEvent::kNoWait);
        ui_.drawLineC(0x18, 0x19, 0x127, 0x19, 4);
        tb() = "Associates: \n ";
        for (int i = 1; i < 16; ++i) {
            const int dist = Sim::cityDistance(static_cast<std::uint16_t>(w().orgs[static_cast<std::size_t>(o)].home),
                                               static_cast<std::uint16_t>(w().orgs[static_cast<std::size_t>(i)].home));
            if (dist >= 8 && dist < 12 && i != o) {
                tb() += orgName(i);
                tb() += "\n ";
            }
        }
        tb() += "none";
        ui_.showMenu(tb(), 0x14, 0x67, SimEvent::kNoSelect);
    }
}

void CityGame::citySummary() {
    for (;;) {
        ui_.clearPlayAreaNow();
        tb() = "Which city ...\n";
        menuCount_ = 0;
        for (int i = 0; i < 16; ++i) {   // append_city_name
            tb() += " ";
            tb() += cityName(i);
            tb() += "\n";
            menuItems_[static_cast<std::size_t>(menuCount_++)] = i;
        }
        int loc = ui_.showMenu(tb(), 0x14, 0x15);
        if (loc == -1) break;
        loc = menuItems_[static_cast<std::size_t>(loc)];
        ui_.drawPageFrame(cityName(loc), 2);
        ui_.drawTextCentered(cityName(loc), 0xA0, 0xF, 5);
        bool any = false;
        tb() = "Known Locations:\n-";
        for (int i = 1; i < 16; ++i) {
            if (!sim_.orgAlertLevel(loc, i)) continue;
            if (sim_.setLocationKnown(i, loc, 0) == -1) continue;
            if (w().sites[static_cast<std::size_t>(sim_.setLocationKnown(i, loc, 0))].flags & 4) {
                tb() += orgName(i);
                sim_.appendBuildingType(loc, i);
            } else {
                tb() += "Unknown building.";
            }
            tb() += "\n-";
            any = true;
        }
        if (!any) tb() += "none\n";
        ui_.showMenu(tb(), 0xA0, 0x19, SimEvent::kNoWait);
        any = false;
        tb() = "Suspects:\n-";
        for (int i = 0; i < 64; ++i) {
            if ((person(i).known & 8) && person(i).city == loc) {
                sim_.appendPersonName(i);
                if (person(i).known & 4) {
                    tb() += "/";
                    tb() += orgAbbrev(person(i).org);
                }
                for (int j = 0; j < h().numSuspects; ++j) {
                    if (person(i).roles & bit16(j)) {
                        tb() += "\n... \x8C";
                        tb() += sim_.suspect(j).name.str();
                    }
                }
                tb() += "\n- ";
                any = true;
            }
        }
        if (!any) tb() += "none\n";
        ui_.showMenu(tb(), 0x14, 0x19, SimEvent::kNoWait);
        ui_.drawLineC(0x18, 0x19, 0x127, 0x19, 10);
        any = false;
        tb() = "Clues:\n";
        for (int i = 0; i < 80; ++i) {
            const Clue& c = w().clues[static_cast<std::size_t>(i)];
            if ((c.kind >> 8) == 8 && (c.kind & 0xFF) == loc) {
                tb() += "-";
                sim_.describePerson(i);
                tb() += "\n";
                any = true;
            }
        }
        if (!any) tb() += "none\n";
        sim_.wrapTextBuffer(16);
        ui_.showMenu(tb(), 0x14, 0x71);
        ui_.clearPlayAreaNow();
    }
}

void CityGame::documentsScreen() {
    int item;
    do {
        ui_.drawPageFrame("Documents", 7);
        tb() = "Select Document:\n ";
        menuItems_[0] = -1;
        menuCount_ = 1;
        for (int i = 0; i < 16; ++i) {
            if (!(static_cast<std::uint16_t>(h().seenOrgs) & bit16(i))) continue;
            menuItems_[static_cast<std::size_t>(menuCount_++)] = i;
            if (i == 0) {
                tb() += "Master Plan\n ";
            } else {
                tb() += orgName(i);
                tb() += " Personel File\n ";
            }
        }
        if (menuCount_ == 1) {
            tb() += "none \n";
            menuItems_[1] = -1;
        }
        item = menuItems_[static_cast<std::size_t>(ui_.showMenu(tb(), 0x14, 0x17) + 1)];
        if (item != -1) showMasterPlan(item, false);
    } while (item != -1);
}

void CityGame::suspectFilesScreen() {
    for (;;) {
        ui_.drawPageFrame("Suspect Files", 7);
        menuCount_ = 1;
        menuItems_[0] = -1;
        const int sel = listSuspects();
        if (sel == -1) break;
        sim_.suspectDossier(sel, 0, 0);
        ui_.waitKey();
    }
}

int CityGame::listSuspects() {
    tb() = " ";
    for (int i = 0; i < h().numPersons && i < 64; ++i) {
        if (!person(i).known) continue;
        sim_.appendPersonName(i);
        if (person(i).known & 4) {
            tb() += " of the ";
            tb() += orgAbbrev(person(i).org);
        }
        if (person(i).known & 8) {
            tb() += " in ";
            tb() += cityName(person(i).city);
        }
        tb() += ".\n ";
        bool flag = false;
        for (int k = 0; k < 15; ++k) {
            if (person(i).roles & bit16(k)) {
                tb() += "\x8C ";
                tb() += sim_.suspect(k).name.str();
                tb() += " ";
                flag = true;
            }
        }
        const int fate = person(i).fate;
        if (fate == -1) tb() += "(Arrested)", flag = true;
        if (fate == -2) tb() += "(Hiding)", flag = true;
        if (fate == -3) tb() += "(Turned)", flag = true;
        if (fate == -4) tb() += "(In Custody)", flag = true;
        menuItems_[static_cast<std::size_t>(menuCount_++)] = i;
        if (flag) {
            tb() += "\n ";
            menuItems_[static_cast<std::size_t>(menuCount_++)] = i;
        }
        if (menuCount_ >= 62) break;
    }
    if (menuCount_ == 1) {
        tb() += "none \n";
        menuItems_[static_cast<std::size_t>(menuCount_)] = -1;
    }
    return menuItems_[static_cast<std::size_t>(ui_.showMenu(tb(), 10, 0x11) + 1)];
}

void CityGame::codedMessagesScreen() {
    ui_.drawPageFrame("Coded Messages", 7);
    menuCount_ = 1;
    menuItems_[0] = -1;
    tb() = " ";
    for (int msg = 0; msg < h().numMsgs && msg < 128; ++msg) {
        const Msg& m = w().msgs[static_cast<std::size_t>(msg)];
        const int md = m.msg;
        if (m.type != 0 || m.flags == 0) continue;
        const int k = sim_.plot(md).flags & 0xF00;
        if (!(k == 0x200 || k == 0x300 || md < 0)) continue;
        if (m.flags & 0x8000) continue;
        sim_.appendPersonName(m.sender);
        tb() += " ";
        const std::size_t row = tb().size();
        sim_.formatMessageEvent(msg);
        std::string txt = row + 11 < tb().size() ? tb().substr(row + 11) : std::string();
        if (txt.size() > 63) txt.resize(63);
        while (!txt.empty() && sim_.textWidth(txt) > 0x124) txt.pop_back();
        tb().resize(std::min(tb().size(), row + 11));
        tb() += "\n ...";
        tb() += txt;
        tb() += "\n ";
        if (menuCount_ < 62) {
            menuItems_[static_cast<std::size_t>(menuCount_++)] = msg;
            menuItems_[static_cast<std::size_t>(menuCount_++)] = msg;
        }
    }
    if (lastExit_) return;
    if (menuCount_ == 1) {
        tb() += " none\n";
        menuItems_[static_cast<std::size_t>(menuCount_)] = -1;
    }
    const int msg = menuItems_[static_cast<std::size_t>(ui_.showMenu(tb(), 10, 0xF) + 1)];
    if (msg != -1) {
        env_[8] = static_cast<std::uint8_t>(msg);
        sim_.composeInterceptText(msg);
        env_.intercept = tb().substr(0, std::min<std::size_t>(tb().size(), 0x200));
    }
}

// ---------------------------------------------------------------- SUSPECTS.C

void CityGame::suspectsOverview() {
    for (;;) {
        ui_.fill(0, 0, 320, 20, 8);
        for (int i = 0; i < h().numSuspects; ++i) sim_.suspect(i).flags &= static_cast<std::uint16_t>(~0x4000u);
        menuCount_ = 0;
        int yo = 6;
        for (int i = 0; i < 80; ++i) {
            const Clue& c = w().clues[static_cast<std::size_t>(i)];
            if (c.suspect == -1) continue;
            const int sd = c.suspect;
            if (sim_.suspect(sd).flags & 0x4000) continue;
            sim_.suspect(sd).flags |= 0x4000;
            if (menuCount_ < 63) menuItems_[static_cast<std::size_t>(menuCount_++)] = c.suspect;
            const int p = sim_.suspect(sd).person;
            tb().clear();
            if (person(p).fate == -1) tb() = "ARRESTED";
            if (person(p).fate == -3) tb() = "TURNED";
            const std::string bufg = tb().substr(0, 9);
            tb().clear();
            sim_.appendNumber(c.text);
            const int colorl = kTabColors[menuCount_ & 3];
            ui_.fill(0, yo + 8, 320, 8, colorl);
            if (menuCount_ & 1) {
                drawTitleTab(ui_.screen, ui_.faces(), 160, yo, static_cast<std::uint8_t>(colorl));
                ui_.fill(172, yo + 1, 134, 9, 7);
                ui_.drawTextCentered(tb(), 240, yo + 2, 0);
                ui_.drawTextCentered(bufg, 240, yo + 6, 12);
            } else {
                drawTitleTab(ui_.screen, ui_.faces(), 0, yo, static_cast<std::uint8_t>(colorl));
                ui_.fill(12, yo + 1, 134, 9, 7);
                ui_.drawTextCentered(tb(), 80, yo + 2, 0);
                ui_.drawTextCentered(bufg, 80, yo + 6, 12);
            }
            yo += 6;
        }
        ui_.fill(0, yo + 10, 320, 190 - yo, 8);
        tb() = "Clues";
        ui_.drawTextT(tb(), 144, yo + 16, 15);
        ui_.drawOutline(142, yo + 14, ui_.font().textWidth(tb()) + 4, 10, 7);
        ui_.drawOutline(16, yo + 12, 288, 24, 0);
        int doneo = 0;
        do {
            const int xn = (listSel_ & 1) ? 12 : 172;
            const int yy = listSel_ * 6 + 7;
            ui_.recolor(xn, yy, 136, 9, 7, 15);
            ui_.wait(20);
            const int key = ui_.getKey();
            switch (key) {
            case 0x5000:
                if (listSel_ < menuCount_ - 1) ++listSel_;
                break;
            case 0x4800:
                if (listSel_ > 0) --listSel_;
                break;
            case 0x0D:
            case 0x20:
                doneo = 1;
                break;
            case 0x1B:
                doneo = -1;
                break;
            default:
                break;
            }
            ui_.recolor(xn, yy, 136, 9, 15, 7);
        } while (doneo == 0);
        ui_.flushInput();
        if (doneo != -1 && listSel_ < menuCount_) {
            sim_.showPersonClues(menuItems_[static_cast<std::size_t>(listSel_)]);
            continue;
        }
        if (doneo != -1) continue;
        break;
    }
}

// ---------------------------------------------------------------- CHRONO.C

void CityGame::chronologyScreen(bool mode) {
    const char* title = mode ? "Chronology" : "News";
    ui_.drawPageFrame(title, 0xE);
    int y = 0x10;
    for (int i = 0; i < h().numMsgs && i < 128; ++i) {
        const Msg& m = w().msgs[static_cast<std::size_t>(i)];
        if (m.type == -1) continue;
        for (;;) {
            const int md = m.msg;
            if (m.type == 0) {
                if (sim_.plot(md).flags & 0x2000) {
                    ui_.drawTextT(sim_.formatDateTime(static_cast<std::uint16_t>(m.time)).substr(9), 8, y, 7);
                    y += 8;
                    tb().clear();
                    sim_.loadMessageText(md);
                    y = sim_.drawWrapped(0x24, 0xC, y, 0);
                } else if (m.flags && mode) {
                    const int msg = m.msg;
                    const int k = sim_.plot(msg).flags & 0xF00;
                    if ((m.flags & 0x8000) && (k == 0x200 || k == 0x800)) {
                        tb() = sim_.suspect(sim_.plot(msg).suspect).name.str();
                        tb() += (sim_.plot(msg).flags & 0x200) ? " sent msg to " : " met with ";
                        tb() += sim_.suspect(sim_.plot(msg).flags & 0xF).name.str();
                        ui_.drawTextT(tb(), 8, y, 7);
                        y += 8;
                        tb() = "'";
                        sim_.loadMessageText(msg);
                        tb() += "'";
                        y = sim_.drawWrapped(0x24, 0xC, y, 0xC);
                    } else if (k == 0x800) {
                        ui_.drawTextT("Meeting Noted", 8, y, 7);
                        y += 8;
                        tb().clear();
                        sim_.loadMessageText(msg);
                        sim_.garbleMessage(m.flags, 5);
                        y = sim_.drawWrapped(0x24, 0xC, y, 0xC);
                    }
                }
            }
            if (y >= 0xC0) {
                ui_.drawText("...more", 0x100, 0xC0, 0xC);
                ui_.waitKey();
                ui_.drawPageFrame(title, 0xE);
                y = 0x10;
                continue;   // (goto again: the same entry on the new page)
            }
            break;
        }
    }
    if (y == 0x10) ui_.drawText("none", 0xC, y, 7);
    ui_.waitKey();
}

// ---------------------------------------------------------------- PERSON.C

void CityGame::showMasterPlan(int org, bool reveal) {
    if (org == 0) {
        h().seenOrgs = static_cast<std::int16_t>(h().seenOrgs | 1);
        ui_.drawPageFrame("Master Plan", 9);
        int y = 0x20;
        tb().clear();
        for (int i = 0; i < 5; ++i) tb() += "--SECRET--";
        ui_.drawTextT(tb(), 12, 0x10, 14);
        ui_.drawTextT(tb(), 12, 0xBC, 14);
        for (int i = 0; i < sim_.activeSuspects(); ++i) {
            if (sim_.suspect(i).flags & 0x100) continue;
            const int x = (i & 3) * 0x48 + 0x1C;
            if (i == 4) y += 0x50;
            const int p = sim_.suspect(i).person;
            if ((person(p).known & 0x81) != 0x80) ui_.drawPortrait(person(p).id, x, y);
            tb().clear();
            if (person(p).fate == -1) tb() = "ARRESTED";
            if (person(p).fate == -2) tb() = "IN HIDING";
            if (person(p).fate == -3) tb() = " TURNED";
            ui_.drawTextT(tb(), x - 1, y + 0x10, 5);
            ui_.drawTextT(tb(), x - 2, y + 0x10, 12);
            tb() = sim_.suspect(i).name.str();
            ui_.drawTextCentered(tb(), x + 0x19, y + 0x38, 12);
            y ^= 8;
        }
    } else {
        h().seenOrgs = static_cast<std::int16_t>(h().seenOrgs | bit16(org));
        ui_.drawPageFrame("Personnel File", 9);
        tb() = orgName(org);
        tb() += " Action Team";
        ui_.drawTextT(tb(), 0x50, 0x10, 5);
        int y = 0x20;
        bool none = true;
        for (int i = 1; i < h().numPersons && i < 64; ++i) {
            if (person(i).org == org && (person(i).flags & 0x7FFF)) {
                if (reveal) person(i).known |= 0x0E;
                tb().clear();
                sim_.appendPersonName(i);
                if (person(i).known & 8) {
                    tb() += ", ";
                    tb() += cityName(person(i).city);
                }
                ui_.drawTextT(tb(), 0x50, y, 12);
                y += 8;
                none = false;
            }
        }
        if (none) ui_.drawTextT("(No activity)", 0x50, y, 12);
    }
    ui_.waitKey();
}

}  // namespace reaction
