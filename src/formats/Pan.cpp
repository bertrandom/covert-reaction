#include "formats/Pan.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "formats/Pic.h"

namespace reaction {

namespace {

// The stream reader of ANIMPAN.C (stream_getc, stream_read), over the whole file.
class PanStream {
public:
    explicit PanStream(std::span<const std::uint8_t> d) : d_(d) {}

    std::uint8_t getc() { return pos_ < d_.size() ? d_[pos_++] : (++pos_, std::uint8_t{0}); }
    int word() {
        const int lo = getc();
        return static_cast<std::int16_t>(lo | getc() << 8);
    }
    // decode_picture / decode_sprite: "if ((stream_end - stream_ptr) & 1)
    // stream_getc()". The 512-byte buffer is filled from offset 0, so this
    // aligns to an even file offset, where the word-reading decoder wants it.
    void align() {
        if (pos_ & 1) getc();
    }
    std::size_t pos() const { return pos_; }
    void seek(std::size_t p) { pos_ = p; }
    std::span<const std::uint8_t> data() const { return d_; }

private:
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
};

// An embedded picture: a .PIC stream when compressed, else read_picture_header
// (skip, width, height) and raw rows.
Image readPicture(PanStream& in, bool compressed) {
    in.align();
    if (compressed) {
        std::size_t end = 0;
        Picture pic = decodePicture(in.data(), in.pos(), &end);
        in.seek(end);
        return std::move(pic.image);
    }
    in.word();
    const int w = in.word(), h = in.word();
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) throw std::runtime_error("PAN: bad picture size");
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.row(y)[x] = in.getc();
    return img;
}

}  // namespace

PanFile PanFile::parse(std::span<const std::uint8_t> data) {
    PanStream in(data);
    PanFile f;
    // pan_check_header
    char magic[4];
    for (char& c : magic) c = static_cast<char>(in.getc());
    f.version = in.getc();
    f.compressed = in.getc() != 0;
    if (std::memcmp(magic, "PANI", 4) != 0) throw std::runtime_error("PAN: not a PANI file");
    if (f.version != 3) throw std::runtime_error("PAN: version " + std::to_string(f.version) + ", expected 3");

    // pan_read_palette
    f.hasPalette = in.getc() != 0;
    if (f.hasPalette) {
        f.paletteType = in.getc();
        const std::size_t n = f.paletteType == 0 ? 17 : f.paletteType == 2 ? 0x306 : 0;
        for (std::size_t i = 0; i < n; ++i) f.paletteData.push_back(in.getc());
    }

    // pan_read_header
    f.x0 = in.word();
    f.y0 = in.word();
    f.x1 = in.word();
    f.y1 = in.word();
    f.frameTicks = in.word();

    // pan_read_background
    switch (in.getc()) {
    case 1:
        f.background = Background::Picture;
        f.backgroundImage = readPicture(in, f.compressed);
        break;
    case 2:
        f.background = Background::Fill;
        f.backgroundColor = in.word();
        break;
    default:
        break;
    }

    // pan_load_sprites
    std::array<int, kImageSlots> present{};
    for (int& p : present) p = in.word();
    for (std::size_t i = 0; i < present.size(); ++i)
        if (present[i]) f.images[i] = readPicture(in, f.compressed);

    // pan_load_script
    const auto paras = static_cast<std::uint16_t>(in.word());
    f.script.resize(static_cast<std::size_t>(paras) * 16);
    for (auto& b : f.script) b = in.getc();
    f.parsedSize = in.pos();
    if (f.parsedSize > data.size()) throw std::runtime_error("PAN: truncated file");
    return f;
}

std::optional<Palette> PanFile::palette() const {
    if (!hasPalette) return std::nullopt;
    if (paletteType == 0 && paletteData.size() == 17) {
        PaletteMap map{};
        std::copy(paletteData.begin(), paletteData.end(), map.begin());
        map[0] = 0;  // anim_set_palette: pan_palette[0] = 0
        return mcgaPalette(map);
    }
    if (paletteType == 2 && paletteData.size() >= 768) {
        // A full DAC (int86x_wrapper loads 256 registers from it). The game's
        // anim_set_pal doesn't handle this type; no Covert Action file uses it.
        Palette p;
        for (std::size_t i = 0; i < 256; ++i)
            p.colors[i] = Palette::fromVga6(paletteData[i * 3], paletteData[i * 3 + 1], paletteData[i * 3 + 2]);
        return p;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------

PanPlayer::PanPlayer(PanFile file, PanOptions options, const Image* screen) : file_(std::move(file)) {
    if (screen && screen->width() == 320 && screen->height() == 200) view_ = back_ = *screen;
    // pan_read_header: the view is page 1, the back page 2, same window.
    viewPort_ = {1, file_.x0, file_.y0, file_.x1, file_.y1};
    // pan_read_background draws on the back page at (0, 0), before the request's
    // position overrides.
    if (file_.background == PanFile::Background::Picture)
        back_.blit(file_.backgroundImage, 0, 0, file_.backgroundImage.width(), file_.backgroundImage.height(), 0, 0);
    else if (file_.background == PanFile::Background::Fill)
        back_.fill(static_cast<std::uint8_t>(file_.backgroundColor));
    // anim_start
    frameTicks_ = options.ticks != -1 ? options.ticks : file_.frameTicks;
    if (options.x != -1) viewPort_.x0 = options.x;
    if (options.y != -1) viewPort_.y0 = options.y;
    backPort_ = viewPort_;
    backPort_.page = 2;
    pageA_ = {2, 0, 0, 319, 199};  // anim_page_a (anim_rp2)
}

Palette PanPlayer::palette() const {
    if (auto p = file_.palette()) return *p;
    return mcgaPalette(palettes::kGame);
}

double PanPlayer::framePeriod() const {
    return std::max(static_cast<double>(frameTicks_) * kBiosTickSeconds, kRetraceSeconds);
}

int PanPlayer::advance(double seconds) {
    clock_ += seconds;
    int n = 0;
    const double period = framePeriod();
    while (clock_ >= period && !done_) {
        clock_ -= period;
        if (step()) ++n;
    }
    if (done_) clock_ = 0;
    return n;
}

void PanPlayer::setVar(int index, int value) {
    if (index >= 0 && index < kVars) vars_[static_cast<std::size_t>(index)] = static_cast<std::int16_t>(value);
}

std::uint8_t PanPlayer::byteAt(std::uint16_t off) const {
    return off < file_.script.size() ? file_.script[off] : 0;
}

std::int16_t PanPlayer::wordAt(std::uint16_t off) const {
    return static_cast<std::int16_t>(byteAt(off) | byteAt(static_cast<std::uint16_t>(off + 1)) << 8);
}

std::int16_t& PanPlayer::stack(int i) {
    if (i < 0 || i >= kStack) throw std::runtime_error("PAN script: stack out of range");
    return stack_[static_cast<std::size_t>(i)];
}

std::int16_t PanPlayer::pop() { return stack(--sp_); }
void PanPlayer::push(std::int16_t v) { stack(sp_++) = v; }

// anim_step / anim_advance(1).
bool PanPlayer::step() {
    if (done_) return false;
    ++shown_;
    for (auto& s : sprites_)  // anim_clear_moved
        if (s.active) s.moved = false;
    if (runScript()) {
        done_ = true;
        return false;
    }
    for (auto& s : sprites_) updateSprite(s);
    // anim_erase_sprites: frozen sprites are drawn into the back page (with
    // blit_sprite_restore, which marks both pages' rows dirty).
    for (const auto& s : sprites_) {
        int x = 0, y = 0;
        if (s.active && s.moved && spritePos(s, x, y))
            if (const Image* img = spriteImage(s)) drawImage(backPort_, x, y, *img);
    }
    presentFrame();
    // anim_draw_sprites: the rest on the view page, in slot order.
    for (const auto& s : sprites_) {
        int x = 0, y = 0;
        if (s.active && s.image != 0xFF && !s.moved && spritePos(s, x, y))
            if (const Image* img = spriteImage(s)) drawImage(viewPort_, x, y, *img);
    }
    // anim_flush_sounds plays the queue from its end.
    sounds_.assign(queued_.rbegin(), queued_.rend());
    queued_.clear();
    pending_.insert(pending_.end(), sounds_.begin(), sounds_.end());
    return true;
}

// anim_present_frame on MCGA (no hardware back buffer: drv_has_back_buffer(0)
// is 0). The first frame copies the back page's top-left (the background
// picture) to the view's window and the result back. Later frames copy the
// back page's dirty rows (where sprites were drawn or frozen) to the view
// (present_dirty_rows); everywhere else in the window the two pages are
// already equal, so copying the whole window gives the same picture.
void PanPlayer::presentFrame() {
    const int w = viewPort_.xmax + 1, h = viewPort_.ymax + 1;
    if (presentMode_ == 2) {
        presentMode_ = 1;
        copyRect(pageA_, 0, 0, w, h, viewPort_, 0, 0);
        copyRect(viewPort_, 0, 0, w, h, backPort_, 0, 0);
    } else {
        presentMode_ = 0;
        copyRect(backPort_, 0, 0, w, h, viewPort_, 0, 0);
    }
}

// drv_draw_image (mgfx_draw_image, clip_image, blit_image): clipped to the
// port's window, colour 0 transparent.
void PanPlayer::drawImage(const Port& port, int x, int y, const Image& img) {
    if (port.xmax < 0 || port.ymax < 0) return;
    Image& dst = page(port.page);
    for (int j = 0; j < img.height(); ++j) {
        const int py = y + j;
        if (py < 0 || py > port.ymax) continue;
        for (int i = 0; i < img.width(); ++i) {
            const int px = x + i;
            if (px < 0 || px > port.xmax) continue;
            if (const std::uint8_t c = img.at(i, j)) dst.set(port.x0 + px, port.y0 + py, c);
        }
    }
}

// drv_copy_rect (mgfx_copy_rect): unclipped in the original; clipped to the page here.
void PanPlayer::copyRect(const Port& src, int sx, int sy, int w, int h, const Port& dst, int dx, int dy) {
    if (src.page == dst.page) {
        if (src.x0 + sx == dst.x0 + dx && src.y0 + sy == dst.y0 + dy) return;
        const Image tmp = page(src.page);
        page(dst.page).blit(tmp, src.x0 + sx, src.y0 + sy, w, h, dst.x0 + dx, dst.y0 + dy);
        return;
    }
    page(dst.page).blit(page(src.page), src.x0 + sx, src.y0 + sy, w, h, dst.x0 + dx, dst.y0 + dy);
}

bool PanPlayer::runScript() {
    // Opcodes the original doesn't know leave anim_pc where it is and loop
    // forever; reported as an error here.
    while (!done_) {
        if (waiting_) {
            // Opcode 2's counter: one frame per count, then carry on in the same frame.
            if (waitCount_-- != 0) return false;
            waiting_ = false;
            continue;
        }
        const auto op = static_cast<std::int8_t>(byteAt(pc_));
        auto at = [&](int k) { return stack(sp_ - k); };
        switch (op) {
        case 0: {  // create sprite
            if (at(6) == -1) stack(sp_ - 6) = static_cast<std::int16_t>(allocSprite());
            const int n = at(6);
            if (n > 0 && n <= kSprites) createSprite(n, at(7), at(5), at(4), at(3), at(2), at(1));
            sp_ -= 7;
            ++pc_;
            break;
        }
        case 1: {  // remove sprite
            const int n = at(1);
            if (n > 0 && n <= kSprites) sprites_[static_cast<std::size_t>(n - 1)].active = false;
            --sp_;
            ++pc_;
            break;
        }
        case 2:  // wait
            waitCount_ = pop();
            waiting_ = true;
            ++pc_;
            break;
        case 3: {  // queue a sound (anim_mem.sounds holds 30)
            const std::int16_t n = pop();
            if (queued_.size() < 30) queued_.push_back(n);
            ++pc_;
            break;
        }
        case 4: {  // freeze sprite for this frame
            const int n = at(1);
            if (n > 0 && n <= kSprites) sprites_[static_cast<std::size_t>(n - 1)].moved = true;
            --sp_;
            ++pc_;
            break;
        }
        case 5: {  // push constant / variable
            std::int16_t v = wordAt(static_cast<std::uint16_t>(pc_ + 2));
            if (byteAt(static_cast<std::uint16_t>(pc_ + 1)))
                v = (v >= 0 && v < kVars) ? vars_[static_cast<std::size_t>(v)] : 0;
            push(v);
            pc_ = static_cast<std::uint16_t>(pc_ + 4);
            break;
        }
        case 6: {  // store variable
            const std::int16_t v = pop();
            const int i = wordAt(static_cast<std::uint16_t>(pc_ + 1));
            if (i >= 0 && i < kVars) vars_[static_cast<std::size_t>(i)] = v;
            pc_ = static_cast<std::uint16_t>(pc_ + 3);
            break;
        }
        case 7:  // dup
            push(at(1));
            ++pc_;
            break;
        case 8: case 9: case 10: case 11: case 12: case 13:
        case 14: case 15: case 16: case 17: {
            const int b = pop();
            std::int16_t& a = stack(sp_ - 1);
            int r = 0;
            switch (op) {
            case 8: r = a == b; break;
            case 9: r = a != b; break;
            case 10: r = a > b; break;
            case 11: r = a < b; break;
            case 12: r = a >= b; break;
            case 13: r = a <= b; break;
            case 14: r = a + b; break;
            case 15: r = a - b; break;
            case 16: r = a * b; break;
            default: r = b ? a / b : 0; break;  // (the original faults on / 0)
            }
            a = static_cast<std::int16_t>(r);
            ++pc_;
            break;
        }
        case 18:  // pop; jump if non-zero
            if (pop() == 0) pc_ = static_cast<std::uint16_t>(pc_ + 3);
            else pc_ = static_cast<std::uint16_t>(wordAt(static_cast<std::uint16_t>(pc_ + 1)));
            break;
        case 19:  // jump
            pc_ = static_cast<std::uint16_t>(wordAt(static_cast<std::uint16_t>(pc_ + 1)));
            break;
        case 20:  // end of frame, without moving on: the script stays here
            return false;
        case 21:  // end
            done_ = true;
            break;
        case 22:  // return
            pc_ = static_cast<std::uint16_t>(pop());
            break;
        case 23:  // call
            push(static_cast<std::int16_t>(pc_ + 3));
            pc_ = static_cast<std::uint16_t>(wordAt(static_cast<std::uint16_t>(pc_ + 1)));
            break;
        default:
            throw std::runtime_error("PAN script: unknown opcode " + std::to_string(op) + " at " +
                                     std::to_string(pc_));
        }
    }
    return true;
}

int PanPlayer::allocSprite() const {
    for (int i = 1; i <= kSprites; ++i)
        if (!sprites_[static_cast<std::size_t>(i - 1)].active) return i;
    return kSprites;  // all busy: the last one is reused
}

// anim_create_sprite. The image is left as it was: a slot reused shows its old
// image until the program's first step (which runs this same frame unless the
// speed is 0).
void PanPlayer::createSprite(int n, int start, int parent, int x0, int y0, int speed, int flags) {
    Sprite& s = sprites_[static_cast<std::size_t>(n - 1)];
    s.active = true;
    s.moved = false;
    s.parent = parent - 1;
    s.x0 = static_cast<std::int16_t>(x0);
    s.y0 = static_cast<std::int16_t>(y0);
    if (s.parent == -2) {
        s.x = s.x0;
        s.y = s.y0;
    } else {
        s.x = s.y = 0;
    }
    s.rate = s.speed = static_cast<std::int16_t>(speed);
    s.accum = 0xFF;
    s.depth = 0;
    s.start = static_cast<std::uint16_t>(start);
    s.flags = static_cast<std::int16_t>(flags);  // only picks the blit variant
    s.pc = s.start;
}

void PanPlayer::updateSprite(Sprite& s) {
    if (!s.active) return;
    s.accum = static_cast<std::int16_t>(s.accum + s.rate);
    if (s.accum <= 0xFF) return;
    s.accum = static_cast<std::int16_t>(s.accum - 0xFF);
    auto arg = [&](int k) { return wordAt(static_cast<std::uint16_t>(s.pc + k)); };
    auto add16 = [](std::int16_t a, int b) { return static_cast<std::int16_t>(a + b); };
    // The original loops until an opcode ends the step; a program that never
    // does would hang it. Give up on such a sprite.
    for (int guard = 0; guard < 100000; ++guard) {
        const auto op = static_cast<std::int8_t>(byteAt(s.pc));
        switch (op) {
        case 0:  // show image, end of step
            s.image = byteAt(static_cast<std::uint16_t>(s.pc + 1));
            s.pc = static_cast<std::uint16_t>(s.pc + 2);
            return;
        case 1:  // move to
            s.x = arg(1);
            s.y = arg(3);
            s.pc = static_cast<std::uint16_t>(s.pc + 5);
            break;
        case 2:  // move by
            s.x = add16(s.x, arg(1));
            s.y = add16(s.y, arg(3));
            s.pc = static_cast<std::uint16_t>(s.pc + 5);
            break;
        case 3:  // set rate
            s.rate = arg(1);
            s.pc = static_cast<std::uint16_t>(s.pc + 3);
            break;
        case 4:  // change rate
            s.rate = add16(s.rate, arg(1));
            s.pc = static_cast<std::uint16_t>(s.pc + 3);
            break;
        case 5:  // loop start
            if (s.depth < 10) s.loops[static_cast<std::size_t>(s.depth++)] = arg(1);
            s.pc = static_cast<std::uint16_t>(s.pc + 3);
            break;
        case 6: {  // loop end
            if (s.depth <= 0) {  // (unbalanced: the original reads loops[-1])
                s.pc = static_cast<std::uint16_t>(s.pc + 3);
                break;
            }
            auto& count = s.loops[static_cast<std::size_t>(s.depth - 1)];
            count = add16(count, -1);
            if (count != 0) {
                s.pc = static_cast<std::uint16_t>(arg(1));
            } else {
                --s.depth;
                s.pc = static_cast<std::uint16_t>(s.pc + 3);
            }
            break;
        }
        case 7:  // reset, then restart
            if (s.parent == -2) {
                s.x = s.x0;
                s.y = s.y0;
            } else {
                s.x = s.y = 0;
            }
            s.rate = s.speed;
            s.accum = 0xFF;
            [[fallthrough]];
        case 8:  // restart
            s.depth = 0;
            s.pc = s.start;
            break;
        case 9:  // stop (stays on this opcode)
            return;
        case 10:  // remove
            s.active = false;
            return;
        default:
            // Unknown opcodes don't advance: the original would spin forever.
            s.active = false;
            return;
        }
    }
    s.active = false;
}

// anim_sprite_pos. The original leaves the caller's (uninitialised) x, y alone
// when the parent is invalid or inactive; such sprites aren't drawn here.
bool PanPlayer::spritePos(const Sprite& s, int& x, int& y) const {
    if (s.parent == -2) {
        x = s.x;
        y = s.y;
        return true;
    }
    if (s.parent < 0 || s.parent >= kSprites) return false;
    const Sprite& p = sprites_[static_cast<std::size_t>(s.parent)];
    if (!p.active) return false;
    x = p.x + s.x0 + s.x;
    y = p.y + s.y0 + s.y;
    return true;
}

const Image* PanPlayer::spriteImage(const Sprite& s) const {
    if (s.image >= PanFile::kImageSlots) return nullptr;
    const Image& img = file_.images[s.image];
    return img.empty() ? nullptr : &img;
}

}  // namespace reaction
