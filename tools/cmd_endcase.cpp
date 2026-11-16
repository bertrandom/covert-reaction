// reaction-tool endcase SEED [DIFFICULTY [DAYS]]: a case played by the simulation
// (makeDemoFinishedCase), then FINAL's debriefing logic (src/game/EndCase.h),
// printing the texts of its pages, the scores and the career afterwards; and a
// save/load round trip of the result (src/game/SaveGame.h).
#include <cstdio>
#include <string>

#include "Tool.h"
#include "formats/TextDb.h"
#include "game/EndCase.h"
#include "game/Fame.h"
#include "game/NewChar.h"
#include "game/SaveGame.h"

using namespace reaction;

namespace {

std::string escaped(const std::string& s) {
    std::string out;
    char buf[8];
    for (unsigned char c : s) {
        if (c == '\n') out += "\\n";
        else if (c < 0x20 || c >= 0x7F) {
            std::snprintf(buf, sizeof buf, "\\x%02X", c);
            out += buf;
        } else out += static_cast<char>(c);
    }
    return out;
}

void printPages(const char* what, const std::vector<Page>& pages) {
    for (std::size_t i = 0; i < pages.size(); ++i) {
        std::printf("%s page %zu:", what, i);
        for (const auto& o : pages[i].ops)
            if (!o.text.empty()) std::printf(" [%s]", escaped(o.text).c_str());
        std::printf("\n");
    }
}

}  // namespace

REACTION_COMMAND("endcase",
              "endcase SEED [DIFFICULTY [DAYS]]: play a case with the simulation, then run the debriefing logic "
              "(timeline, efficiency report, MasterMind clue, career, promotion) and print it",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.empty()) return 2;
                  const auto seed = static_cast<std::uint32_t>(std::stoul(args[0], nullptr, 0));
                  const int diff = args.size() > 1 ? std::stoi(args[1]) : 1;
                  const int days = args.size() > 2 ? std::stoi(args[2]) : 12;
                  GameState g;
                  makeDemoFinishedCase(g, d, seed, diff, days);
                  std::printf("case: plot %d episode %d, crime %d, %d messages, difficulty %d\n", g.career.plotNum,
                              g.career.lastEpisode, g.career.episodeCrime, g.world.hdr.numMsgs,
                              g.world.hdr.difficulty);
                  g.rng.seed(seed);   // FINAL's srand
                  EndOfCase ec(g, d);
                  ec.begin();
                  printPages("timeline", ec.timeline());
                  printPages("report", ec.efficiencyReport());
                  std::printf("ep %d, case_result %02X, participants %d\n", ec.ep(), ec.caseResult(),
                              ec.numParticipants());
                  auto plot = TextDb::load(d, "plot.txt");
                  std::printf("story %s (var %d): %zu pages\n", ec.storyKey().c_str(), ec.storyVar(),
                              storyPages(plot, ec.storyKey()).size());
                  if (auto t = ec.afterStory()) {
                      std::printf("interrogation: %s\n", escaped(*t).c_str());
                      for (const auto& s : ec.mastermindClue()) {
                          printPages("dossier", s.dossier);
                          if (s.notice) std::printf("notice: %s\n", escaped(*s.notice).c_str());
                      }
                  }
                  if (auto t = ec.capturedText()) std::printf("captured: %s\n", escaped(*t).c_str());
                  const int v = ec.vacationIndex();
                  const Vacation vac = vacationFor(v);
                  std::printf("vacation %d (kind %d), extra %d\n", v, vac.kind, ec.vacationExtra(vac.kind) ? 1 : 0);
                  ec.recordCase();
                  printPages("career", ec.careerSummary());
                  if (auto t = ec.promotion()) std::printf("promotion: %s\n", escaped(*t).c_str());
                  std::printf("month %d, points %d, rank %d, mm known %04X\n", g.career.curMonth,
                              g.career.careerPoints, g.career.careerRank, g.career.mm.known);
                  std::printf("menu: %s\n", escaped(ec.continueMenu()).c_str());

                  ec.beforeSave();
                  auto bytes = savegame::saveBytes(g);
                  GameState g2;
                  const bool ok = savegame::loadBytes(g2, bytes);
                  auto bytes2 = savegame::saveBytes(g2);
                  // load_env clears 80h in the options; put it back to compare.
                  g2.world.hdr.options = g.world.hdr.options;
                  bytes2 = savegame::saveBytes(g2);
                  std::printf("save: %zu bytes, reload %s, round trip %s\n", bytes.size(), ok ? "ok" : "FAILED",
                              bytes == bytes2 ? "identical" : "DIFFERENT");
                  std::printf("%s", savegame::slotListing(bytes).c_str());
                  return ok && bytes == bytes2 ? 0 : 1;
              });
