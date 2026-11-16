// redux-tool text FILE KEY [$NAME=value ...]   and   text-keys FILE
#include <cstdio>

#include "Tool.h"
#include "formats/TextDb.h"

using namespace redux;

REDUX_COMMAND("text", "text FILE KEY [$NAME=value ...]: look up a keyed text entry (TEXT.DTA, CLUES.TXT, ...)",
              [](const GameData& d, const std::vector<std::string>& a) {
                  if (a.size() < 2) return 2;
                  std::vector<Substitution> subst;
                  for (std::size_t i = 2; i < a.size(); ++i)
                      if (auto eq = a[i].find('='); eq != std::string::npos)
                          subst.emplace_back(a[i].substr(0, eq), a[i].substr(eq + 1));
                  auto db = TextDb::load(d, a[0]);
                  auto t = db.lookup(a[1], subst);
                  std::printf("%s\n", t ? t->c_str() : TextDb::missing(a[1]).c_str());
                  return t ? 0 : 1;
              });

REDUX_COMMAND("text-keys", "text-keys FILE: list the keys of a keyed text file",
              [](const GameData& d, const std::vector<std::string>& a) {
                  if (a.empty()) return 2;
                  for (const auto& k : TextDb::load(d, a[0]).keys()) std::printf("%s\n", k.c_str());
                  return 0;
              });
