// .PAN animations: a background, up to 250 sprite images and a bytecode script,
// played by the MicroProse animation player (covert-decomp src/MPSLIB/ANIMPAN.C
// pan_open .. decode_sprite for the file, ANIMOBJ.C anim_play .. anim_update_sprite
// for the player).
//
// File layout (bytes; words little-endian):
//   "PANI", version (3, anim_version), compressed (1 in every file)
//   has_palette; if set, palette_type then: 0 -> 17 bytes (a set_palette map,
//     see McgaPalette.h), 1 -> nothing, 2 -> 0x306 bytes (256 RGB DAC entries,
//     then two more; unused by Covert Action)
//   x0, y0, x1, y1 (word each): the window, x1/y1 its last column/row relative
//     to x0/y0; frame_ticks (word): BIOS timer ticks per frame
//   background type (byte): 0 none, 1 picture, 2 fill colour (word)
//   250 words: non-zero for each sprite image present
//   the background picture and the present images, each starting at an even
//     offset (decode_picture/decode_sprite skip a byte if needed); compressed
//     ones are .PIC streams (Pic.h), uncompressed ones a 6-byte header (skip,
//     width, height) and width bytes per row
//   script size in 16-byte paragraphs (word), then the script.
//
// The script (anim_run_script) is a stack machine of 16-bit ints; addresses are
// offsets in the script:
//    0 create sprite: pops start, n, parent, x0, y0, speed, flags (pushed in that
//      order); n = -1 picks a free slot (1..50); parent 1..50 or -1 (none)
//    1 n: remove sprite         2 n: wait n frames          3 n: queue sound n
//    4 n: freeze sprite n for this frame (stamped into the background, not drawn)
//    5 f w: push w (f != 0: variable w)    6 w: pop into variable w (0..50)
//    7 dup   8 ==  9 !=  10 >  11 <  12 >=  13 <=  14 +  15 -  16 *  17 /
//   18 a: pop, jump to a if non-zero      19 a: jump      23 a: call (pushes
//      the return address)                22: return (pops it)
//   20: stay here, ending the frame (the script stops; its sprites go on)
//   21: end of the animation
// Each sprite runs its own program from `start` (anim_update_sprite): its rate
// is added to an accumulator each frame and a step runs when it passes 255:
//    0 i: show image i, end of step   1 x y: move to   2 dx dy: move by
//    3 r: set rate   4 dr: add to rate   5 n: loop n times   6 a: loop back to a
//    7: reset position/rate and restart  8: restart  9: stop  10: remove sprite
// Sprites with a parent are drawn at parent.(x, y) + (x0, y0) + (x, y).
//
// Pacing (anim_step, anim_frame_delay): one script/sprite step per frame, a frame
// every frame_ticks ticks of the 18.2 Hz BIOS clock (and at least one 70 Hz
// vertical retrace: anim_advance ends with wait_retrace(1)).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/Image.h"
#include "formats/McgaPalette.h"

namespace redux {

struct PanFile {
    enum class Background : std::uint8_t { None, Picture, Fill };
    static constexpr int kImageSlots = 250;

    int version = 0;
    bool compressed = true;
    bool hasPalette = false;
    int paletteType = 0;
    std::vector<std::uint8_t> paletteData;  // 17 bytes (type 0) or 0x306 (type 2)
    int x0 = 0, y0 = 0, x1 = 319, y1 = 199;
    int frameTicks = 1;
    Background background = Background::None;
    Image backgroundImage;    // Background::Picture, drawn at (0, 0) of the back page
    int backgroundColor = 0;  // Background::Fill
    std::vector<Image> images = std::vector<Image>(kImageSlots);  // empty(): absent
    std::vector<std::uint8_t> script;
    std::size_t parsedSize = 0;  // bytes consumed (the script ends there)

    // Throws std::runtime_error if the data isn't a version-3 PAN.
    static PanFile parse(std::span<const std::uint8_t> data);

    // The palette the player sets (anim_set_palette): the file's map with
    // colour 0 forced to black, or nullopt when it carries none (the screen
    // keeps the caller's).
    std::optional<Palette> palette() const;
};

// Overrides of an animation request (anim_req in anim_start); -1: the file's.
struct PanOptions {
    int x = -1, y = -1;  // window position
    int ticks = -1;      // BIOS ticks per frame
};

// The animation player, without timing or output: step() composes the next
// frame into frame() (a 320x200 page) the way the MCGA build does.
class PanPlayer {
public:
    static constexpr int kSprites = 50;
    static constexpr int kVars = 51;
    static constexpr int kStack = 1000;
    static constexpr double kBiosTickSeconds = 65536.0 / 1193182.0;  // 18.2065 Hz
    static constexpr double kRetraceSeconds = 1.0 / 70.086;          // mode 13h refresh

    // `screen`, if given, is what the pages held before (the window is drawn
    // over it; outside the window it stays as it is).
    explicit PanPlayer(PanFile file, PanOptions options = {}, const Image* screen = nullptr);

    // One frame (anim_advance(1)). Returns false, leaving frame() as it was,
    // once the script has ended (opcode 21).
    bool step();
    bool finished() const { return done_; }

    const Image& frame() const { return view_; }
    // The palette to show frame() with: the file's, else the game palette.
    Palette palette() const;
    // Sounds the last step started, in the order the original plays them.
    const std::vector<int>& sounds() const { return sounds_; }

    int frameTicks() const { return frameTicks_; }
    double framePeriod() const;  // seconds per frame
    // Real-time pacing: runs the frames due after `seconds` more seconds;
    // returns how many were composed.
    int advance(double seconds);

    int framesShown() const { return shown_; }
    // set_scene_var: lets the game pass values to the script.
    void setVar(int index, int value);

    const PanFile& file() const { return file_; }

private:
    // A RastPort: a page (1: the view, 2: the back page) and a window on it.
    struct Port {
        int page;
        int x0, y0, xmax, ymax;
    };
    // struct sprite in ANIMOBJ.C.
    struct Sprite {
        bool active = false;
        bool moved = false;  // frozen this frame (opcode 4)
        int parent = -2;     // index of the parent, -2: none
        std::int16_t x0 = 0, y0 = 0, x = 0, y = 0;
        std::int16_t speed = 0, rate = 0, accum = 0;
        std::array<std::int16_t, 10> loops{};
        int depth = 0;
        std::uint16_t start = 0, pc = 0;
        std::int16_t flags = 0;
        std::uint8_t image = 0;  // 0xFF: hidden
    };

    std::uint8_t byteAt(std::uint16_t off) const;
    std::int16_t wordAt(std::uint16_t off) const;
    std::int16_t& stack(int i);
    std::int16_t pop();
    void push(std::int16_t v);

    bool runScript();  // anim_run_script: true when the animation has ended
    void createSprite(int n, int start, int parent, int x0, int y0, int speed, int flags);
    int allocSprite() const;
    void updateSprite(Sprite& s);
    bool spritePos(const Sprite& s, int& x, int& y) const;
    const Image* spriteImage(const Sprite& s) const;
    void presentFrame();
    Image& page(int n) { return n == 1 ? view_ : back_; }
    void drawImage(const Port& port, int x, int y, const Image& img);
    void copyRect(const Port& src, int sx, int sy, int w, int h, const Port& dst, int dx, int dy);

    PanFile file_;
    Image view_{320, 200}, back_{320, 200};  // pages 1 and 2
    Port viewPort_{}, backPort_{}, pageA_{};
    int frameTicks_ = 1;

    std::array<Sprite, kSprites> sprites_{};
    std::array<std::int16_t, kStack> stack_{};
    std::array<std::int16_t, kVars> vars_{};
    int sp_ = 0;
    std::uint16_t pc_ = 0;
    bool waiting_ = false;
    std::int16_t waitCount_ = 0;
    bool done_ = false;
    int presentMode_ = 2;  // 2: first frame, then 1, 0
    std::vector<int> queued_, sounds_;
    int shown_ = 0;
    double clock_ = 0;
};

}  // namespace redux
