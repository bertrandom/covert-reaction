// The colours the game shows on MCGA/VGA (mode 13h), from the MCGA driver
// (MGRAPHIC.EXE, covert-decomp src/DRIVERS/MGRAPHIC.ASM).
//
// Pictures (.PIC) and animations (.PAN) carry no RGB values: they are drawn in
// colours 0..15, and the driver turns those into DAC values:
//   - init_mode (load_default_dac) loads DAC 0..127 from the driver's
//     `default_dac` table (6-bit R,G,B): 0..15 are the 16 EGA colours, 16..127
//     shade ramps that nothing in the data files uses;
//   - set_palette(pal) (slot 26) takes 17 colour numbers: DAC i (0..15) becomes
//     default_dac[pal[i]], and pal[16], if equal to one of pal[0..15], selects
//     that register as the border (overscan) colour.
// The game's palettes are such 17-byte tables (game_palette, face_palette, ...);
// .PAN files carry their own (pan_read_palette). set_color_map (slot 27), the
// CGA/EGA dithering maps, does nothing on MCGA.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/Image.h"

namespace reaction {

// A set_palette argument: 16 colour numbers (indices into default_dac) + border.
using PaletteMap = std::array<std::uint8_t, 17>;

namespace palettes {
// The hardware's own EGA order (what init_mode leaves in DAC 0..15).
inline constexpr PaletteMap kEga = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};
// game_palette (GAME.C, FINAL.C) and text_palette (CHASE.C): colour 5 (magenta)
// shows as black; it's the "blank" colour the game fills screens with. The
// .PAN files carry this same table.
inline constexpr PaletteMap kGame = {0, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};
// face_palette (NEWCHAR.C): as kGame, and colour 13 shows as white.
inline constexpr PaletteMap kFace = {0, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11, 12, 15, 14, 15, 0};
// chase_palette (CHASE.C): colour 0 shows as yellow (the road scene's sky/lines).
inline constexpr PaletteMap kChase = {14, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};

// Looks a preset up by name ("ega", "game", "face", "chase"); nullptr if unknown.
const PaletteMap* byName(std::string_view name);
}  // namespace palettes

// default_dac: the driver's 128 DAC entries, 6 bits per channel (R, G, B).
std::span<const std::uint8_t, 128 * 3> mcgaDefaultDac();

// The 256-colour palette after init_mode + set_palette(map): 0..15 from the map,
// 16..127 the default DAC, 128..255 black (never loaded by the driver).
Palette mcgaPalette(const PaletteMap& map = palettes::kGame);

// The border register set_palette selects (-1: pal[16] isn't among pal[0..15]).
int mcgaBorderColor(const PaletteMap& map);

}  // namespace reaction
