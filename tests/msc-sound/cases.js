// The differential test's cases, shared by run.sh (RUN.BAT for DOSBox-X) and
// the comparison with `reaction-tool sound-events`.
//   node cases.js bat DIR      writes DIR/RUN.BAT
//   node cases.js compare DIR REACTION_TOOL DATA_DIR   compares DIR/*.TXT with the port
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const drivers = { R: ["RSOUND.CA", "mt32", 54], A: ["ASOUND.CA", "adlib", 46], I: ["ISOUND.CA", "speaker", 46] };
// The ISR each driver installs -> the timer divisor reaction-tool prints.
const isrs = { R: { "029D": "4C90" }, A: { "1036": "4C90", "0F80": "0F88" }, I: { "05FC": "4C90", "05C2": "0F88" } };

function cases() {
  const out = [];
  for (const d of Object.keys(drivers)) {
    const max = drivers[d][2];
    for (let n = 0; n <= max; ++n) {
      // ISOUND's 28 waits in play_sound for the effect's end (wait_sound_end):
      // with the timer driven from here, it never returns.
      if (d === "I" && n === 28) continue;
      const ticks = n === 2 ? 10000 : n >= 40 && n <= 43 ? 9000 : 3000;
      out.push({ d, ticks, plays: [String(n)] });
    }
    out.push({ d, ticks: 900, plays: ["2", "37@300", "38@500"] });               // pause, resume
    out.push({ d, ticks: 500, plays: ["26,1", "26,2@60", "26,3@120", "27,2@180", "26,0@240", "26,1@300", "1@400"] });
    out.push({ d, ticks: 600, plays: ["35", "36@200", "31@300"] });
    out.push({ d, ticks: 900, plays: ["2", "3@100", "13@130", "5@200", "1@400", "47@600"] });
    if (d !== "I") {
      for (const [a, b] of [[1, 1], [2, 3], [3, 2], [4, 1], [5, 1], [5, 3]])
        out.push({ d, ticks: 3000, plays: [`44,${a},${b}`] });
      out.push({ d, ticks: 3000, plays: ["44,5,1", "44,4,1@600", "44,2,2@1200", "44,5,2@1800", "44,0,0@2400"] });
    }
  }
  // SND_DRIVERS=RA etc. runs some drivers only.
  const only = process.env.SND_DRIVERS || "RAI";
  return out.map((c, i) => ({ ...c, file: `${c.d}${String(i).padStart(3, "0")}.TXT` }))
            .filter((c) => only.includes(c.d));
}

function normalize(text, d) {
  return text.replace(/\r/g, "").replace(/\(\d+Hz\)/g, "").replace(/\[([0-9A-F]{4})\]/g, (m, isr) => {
    const map = isrs[d];
    return "[" + (map[isr] || (Object.values(map).includes(isr) ? isr : "?" + isr)) + "]";
  }).trimEnd();
}

const [, , cmd, dir, tool, data] = process.argv;
if (cmd === "bat") {
  const lines = ["@ECHO OFF", "CL /AS /Gs /Oas SNDDUMP.C > D:\\CL.LOG"];
  for (const c of cases())
    lines.push(`SNDDUMP ${c.d} E:\\${drivers[c.d][0]} ${c.ticks} ${c.plays.join(" ")} > D:\\${c.file}`);
  fs.writeFileSync(path.join(dir, "RUN.BAT"), lines.join("\r\n") + "\r\n");
} else if (cmd === "compare") {
  let ok = 0, bad = 0;
  for (const c of cases()) {
    const f = path.join(dir, c.file);
    const orig = fs.existsSync(f) ? normalize(fs.readFileSync(f, "latin1"), c.d) : "<missing>";
    const port = normalize(execFileSync(tool, ["--data", data, "sound-events", drivers[c.d][1], ...c.plays,
                                               "--ticks", String(c.ticks), "--init"], { maxBuffer: 1 << 28 }).toString(), c.d);
    if (orig === port) { ++ok; continue; }
    ++bad;
    const a = orig.split("\n"), b = port.split("\n");
    let i = 0;
    while (i < a.length && a[i] === b[i]) ++i;
    console.log(`DIFF ${c.file} (${c.d} ${c.plays.join(" ")}) at line ${i + 1}:\n  orig: ${(a[i] || "<end>").slice(0, 300)}\n  port: ${(b[i] || "<end>").slice(0, 300)}`);
  }
  console.log(`${ok} identical, ${bad} different`);
  process.exit(bad ? 1 : 0);
}
