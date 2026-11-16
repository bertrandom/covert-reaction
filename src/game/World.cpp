#include "game/World.h"

#include "core/Bytes.h"

namespace redux {

namespace {

template <std::size_t N>
void rd(ByteReader& r, FixedString<N>& s) {
    for (auto& c : s.raw) c = static_cast<char>(r.u8());
}
template <std::size_t N>
void wr(ByteWriter& w, const FixedString<N>& s) {
    for (char c : s.raw) w.u8(static_cast<std::uint8_t>(c));
}

// One reader/writer pair per table; `io` is a ByteReader or ByteWriter and the
// same field list drives both directions.
struct In {
    ByteReader& r;
    void operator()(std::int16_t& v) { v = r.s16(); }
    void operator()(std::uint16_t& v) { v = r.u16(); }
    void operator()(std::int8_t& v) { v = r.s8(); }
    void operator()(std::uint8_t& v) { v = r.u8(); }
    template <std::size_t N> void operator()(FixedString<N>& s) { rd(r, s); }
};
struct Out {
    ByteWriter& w;
    void operator()(const std::int16_t& v) { w.s16(v); }
    void operator()(const std::uint16_t& v) { w.u16(v); }
    void operator()(const std::int8_t& v) { w.s8(v); }
    void operator()(const std::uint8_t& v) { w.u8(v); }
    template <std::size_t N> void operator()(const FixedString<N>& s) { wr(w, s); }
};

#define REDUX_FIELDS(T, ...)                                   \
    template <class IO, class X>                               \
        requires std::is_same_v<std::remove_const_t<X>, T>     \
    void table(IO& io, X& p) { __VA_ARGS__; }

REDUX_FIELDS(Person, io(p.org); io(p.id); io(p.city); io(p.known); io(p.flags); io(p.roles); io(p.points); io(p.fate))
REDUX_FIELDS(Site, io(p.org); io(p.city); io(p.flags); io(p.strength); io(p.f8); io(p.fa))
REDUX_FIELDS(Clue, io(p.org); io(p.city); io(p.suspect); io(p.kind); io(p.text); io(p.pad0); io(p.pad1))
REDUX_FIELDS(City, io(p.name); io(p.country); io(p.code); io(p.size); io(p.orgsKnown); io(p.orgsVisited); io(p.mapX); io(p.mapY))
REDUX_FIELDS(Msg, io(p.type); io(p.time); io(p.msg); io(p.flags); io(p.level); io(p.sender); io(p.senderCity); io(p.receiver); io(p.receiverCity))
REDUX_FIELDS(Plot, io(p.suspect); io(p.f1); io(p.state); io(p.msg); io(p.name); io(p.flags); io(p.items); io(p.severity))
REDUX_FIELDS(Suspect, io(p.person); io(p.importance); io(p.name); io(p.info); io(p.f25); io(p.flags); io(p.found); io(p.type); io(p.rank); io(p.f2e))
REDUX_FIELDS(SuspectState, io(p.time); io(p.progress); io(p.f4))
REDUX_FIELDS(Org, io(p.abbrev); io(p.name); io(p.home); io(p.strength); io(p.range); io(p.flags); io(p.nationality); io(p.f23))
REDUX_FIELDS(Item, io(p.name); io(p.icon); io(p.holder))
REDUX_FIELDS(WorldHeader,
    io(p.hereCity); io(p.hereOrg); io(p.options); io(p.msgDay); io(p.org2); io(p.org3); io(p.city1);
    io(p.city2); io(p.clockBase); io(p.numPersons); io(p.numSites); io(p.gameTime); io(p.var38);
    io(p.difficulty); io(p.randSeed); io(p.numSuspects); io(p.numPlots); io(p.jailbreakPerson);
    io(p.videoMode); io(p.numMsgs); io(p.var48); io(p.doubleAgents); io(p.seenOrgs);
    io(p.skillLevels); io(p.var50); io(p.var52))
#undef REDUX_FIELDS

template <class T> void readAll(ByteReader& r, T& range) { In in{r}; for (auto& x : range) table(in, x); }
template <class T> void writeAll(ByteWriter& w, const T& range) { Out out{w}; for (const auto& x : range) table(out, x); }

// The header fields start at ENV+20h, i.e. 10h into the saved env block.
constexpr std::size_t kHeaderOffset = 0x10;

}  // namespace

World World::loadSave(std::span<const std::uint8_t> bytes) {
    World w;
    ByteReader r(bytes);
    for (auto& b : w.env) b = r.u8();
    {
        ByteReader h(w.env, kHeaderOffset);
        In in{h};
        table(in, w.hdr);
    }
    for (auto& b : w.playerBlock) b = r.u8();
    w.suspects.resize(static_cast<std::size_t>(std::max<int>(0, w.hdr.numSuspects)));
    w.suspectState.resize(w.suspects.size());
    w.plots.resize(static_cast<std::size_t>(std::max<int>(0, w.hdr.numPlots)));
    readAll(r, w.suspects);
    readAll(r, w.suspectState);
    readAll(r, w.plots);
    readAll(r, w.cities);
    readAll(r, w.orgs);
    readAll(r, w.persons);
    readAll(r, w.sites);
    readAll(r, w.clues);
    readAll(r, w.msgs);
    readAll(r, w.items);
    for (auto& c : w.cityClueCount) c = r.s16();
    return w;
}

std::vector<std::uint8_t> World::save() const {
    ByteWriter hdrBytes;
    {
        Out out{hdrBytes};
        table(out, hdr);
    }
    auto env2 = env;
    std::copy(hdrBytes.bytes().begin(), hdrBytes.bytes().end(), env2.begin() + kHeaderOffset);
    ByteWriter w;
    for (auto b : env2) w.u8(b);
    for (auto b : playerBlock) w.u8(b);
    writeAll(w, suspects);
    writeAll(w, suspectState);
    writeAll(w, plots);
    writeAll(w, cities);
    writeAll(w, orgs);
    writeAll(w, persons);
    writeAll(w, sites);
    writeAll(w, clues);
    writeAll(w, msgs);
    writeAll(w, items);
    for (auto c : cityClueCount) w.s16(c);
    return std::move(w.bytes());
}

void World::loadCrime(const GameData& data, int n) {
    auto bytes = data.read("CRIME" + std::to_string(n) + ".DTA");
    ByteReader r(bytes);
    hdr.numSuspects = r.s16();
    hdr.numPlots = r.s16();
    suspects.assign(static_cast<std::size_t>(std::max<int>(0, hdr.numSuspects)), {});
    suspectState.assign(suspects.size(), {});
    plots.assign(static_cast<std::size_t>(std::max<int>(0, hdr.numPlots)), {});
    readAll(r, suspects);
    readAll(r, plots);
    readAll(r, items);   // 72 bytes = 4 items
}

void World::loadWorld(const GameData& data, int n) {
    auto bytes = data.read("WORLD" + std::to_string(n) + ".DTA");
    ByteReader r(bytes);
    int nc = r.s16(), norgs = r.s16();
    for (int i = 0; i < nc && i < 16; ++i) { In in{r}; table(in, cities[static_cast<std::size_t>(i)]); }
    for (int i = 0; i < norgs && i < 16; ++i) { In in{r}; table(in, orgs[static_cast<std::size_t>(i)]); }
}

}  // namespace redux
