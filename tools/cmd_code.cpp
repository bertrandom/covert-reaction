// redux-tool code [SEED [LEVEL [DIFFICULTY]]]: the code-breaking puzzle a practice
// run generates (game/CodeBreak), with its answer.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Tool.h"
#include "game/CodeBreak.h"

using namespace redux;

REDUX_COMMAND("code", "code [SEED [LEVEL [DIFFICULTY]]]: a practice crypto puzzle and its answer",
              [](const GameData& d, const std::vector<std::string>& a) {
                  CodeSetup setup;
                  setup.seed = a.size() > 0 ? static_cast<std::uint32_t>(std::stoul(a[0])) : 1u;
                  setup.gameDifficulty = a.size() > 1 ? std::stoi(a[1]) : 0;
                  if (a.size() > 2) setup.difficultyOverride = std::stoi(a[2]);
                  Rng finalRng(setup.seed);
                  setup.message = composePracticeIntercept(d, finalRng);
                  CodeBreak p(setup);
                  auto printable = [](const std::string& s) {
                      std::string o;
                      for (unsigned char c : s) o += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '~';
                      return o;
                  };
                  std::printf("difficulty %d, %d characters\n%s\n", p.difficulty(), p.length(),
                              p.title().c_str());
                  std::printf("intercept: %s\n", printable(setup.message).c_str());
                  std::string plain, cipher;
                  for (int i = 0; i < p.length(); ++i) {
                      plain += static_cast<char>(p.plainAt(i));
                      cipher += static_cast<char>(p.cipherAt(i));
                  }
                  std::printf("plain:  %s\ncipher: %s\n", printable(plain).c_str(), printable(cipher).c_str());
                  std::string keys;
                  std::printf("cipher -> plain (count, shown):\n");
                  for (int n = 0; n < CodeBreak::kLetters; ++n) {
                      if (p.count(n) == 0) continue;
                      std::printf("  %c -> %c (%d)%s\n", 'A' + n, p.answer(n), p.count(n),
                                  p.guess(n) != ' ' ? " revealed" : "");
                      keys += static_cast<char>('a' + n);
                      keys += static_cast<char>(p.answer(n) - 'A' + 'a');
                  }
                  std::printf("keys: %s\n", keys.c_str());
                  return 0;
              });
