// BUG's intercepts during a case. See BugCase.h.
#include "game/BugCase.h"

#include "formats/Pic.h"
#include "formats/TextWindow.h"
#include "game/Paper.h"

namespace reaction {

// msgs[-1] and msgs[128] in BUG's DGROUP (msgs at 6058h, cities at 5E32h):
// msgs[-1] is the end of cities[15] (.time its orgs_known, .flags its map_x and
// map_y), show_ok, num_persons and car_tracer (.receiver, .receiver_city);
// msgs[128] (6758h) face_shown, progress (.time), ticks, popup_flag (.flags)
// and plots[0]'s first bytes (.receiver_city: the high byte of plots[0].msg).
class BugCaseHooks::Neighbours : public SimNeighbours {
public:
    explicit Neighbours(BugCaseHooks& h) : h_(h) {}
    std::uint16_t msgFlags(int i) override {
        if (i == -1) {
            const City& c = h_.w_.cities[15];
            return static_cast<std::uint16_t>(c.mapX | c.mapY << 8);
        }
        if (i == 128) return static_cast<std::uint16_t>(h_.sim_->globals().popupFlag);
        return 0;
    }
    void orMsgFlags(int i, unsigned v) override {
        if (i == -1) {
            City& c = h_.w_.cities[15];
            c.mapX = static_cast<std::uint8_t>(c.mapX | (v & 0xFF));
            c.mapY = static_cast<std::uint8_t>(c.mapY | (v >> 8 & 0xFF));
        }
        if (i == 128) h_.sim_->globals().popupFlag = static_cast<std::uint16_t>(h_.sim_->globals().popupFlag | v);
    }
    std::int16_t msgTime(int i) override {
        if (i == -1) return static_cast<std::int16_t>(h_.w_.cities[15].orgsKnown);
        if (i == 128 && h_.wt_) return static_cast<std::int16_t>(h_.wt_->result().progress);
        return 0;
    }
    int msgReceiverCity(int i) override {
        if (i == 128 && !h_.w_.plots.empty())
            return static_cast<std::int8_t>(static_cast<std::uint16_t>(h_.w_.plots[0].msg) >> 8);
        return 0;   // (-1: the high byte of car_tracer)
    }

private:
    BugCaseHooks& h_;
};

BugCaseHooks::BugCaseHooks(World& world, const GameData& data, Career& career)
    : w_(world), data_(data), career_(career), nb_(std::make_unique<Neighbours>(*this)) {
    ui_.trace = &trace;
    // main(): camera.pic's two images; load_bug_sprites: draw_face
    const Picture cam = decodePicture(data_.read("camera.pic"));
    camera_ = Image(146, 102);
    camera_.blit(cam.image, 0, 0, 146, 102, 0, 0);
    camera2_ = Image(72, 61);
    camera2_.blit(cam.image, 200, 0, 72, 61, 0, 0);
    faces_ = FaceSprites::load(data_);
}

BugCaseHooks::~BugCaseHooks() = default;

int BugCaseHooks::intercept(Wiretap& wt, int amount) {
    wt_ = &wt;
    if (!sim_) {
        sim_ = std::make_unique<Sim>(w_, wt.rng(), data_, career_);
        sim_->setUi(&ui_);
        ui_.sim = sim_.get();
        // persons[-1]: the last 16 bytes of logic[12][4][2] (BUG.C), {1,1},{1,0},{0,1},{0,0}
        Person m1;
        m1.org = 1;
        m1.id = 1;
        m1.city = 1;
        m1.known = 0;
        m1.flags = 0;
        m1.roles = 1;
        m1.points = 0;
        m1.fate = 0;
        sim_->setProgramLibrary(m1, nb_.get());
        // BUG's draw_intercept_popup (popup_x 160, popup_y 100)
        ui_.popup = [this](const SimEvent& e) {
            int xs = clampTo(0x1F, 160 - 0x92, 160), ys = clampTo(0x31, 100 - 0x66, 100);
            const int face = e.index;
            if (face > -1) sim_->globals().faceShown = -1;   // draw_portrait's draw_face_parts
            const bool style1 = sim_->globals().popupFlag == 1;
            std::vector<int> widths;
            if (style1) {
                int off = 16;
                do {
                    const int wv = wt_->rng().below(16) + 2;   // random(16) + 2
                    widths.push_back(wv);
                    off += wv + 4;
                } while (off < 0x72);
            }
            const std::string text = e.text;
            ui_.draw([this, xs, ys, face, style1, widths, text](LibCanvas& c) {
                c.img.blit(camera_, 0, 0, camera_.width(), camera_.height(), xs, ys, 0);
                if (face > -1) drawPortrait(c.img, c.faces, face, xs + 0x3A, ys + 0x18);
                if (face == -2) c.img.blit(camera2_, 0, 0, camera2_.width(), camera2_.height(), xs + 0x3E, ys + 10, 0);
                c.fillcolor = 11;
                drawWrappedText(c.img, c.font, text, 14, xs + 20, ys + 0x43, 3);
                drawWrappedText(c.img, c.font, text, 14, xs + 19, ys + 0x43, 5);
                if (!style1) {
                    drawRectOutline(c.img, xs + 13, ys + 10, 0x78, 0x4E, 15);
                    return;
                }
                int off = 16;
                for (int wv : widths) {
                    drawLine(c.img, xs + off, ys + 0x40, xs + off + wv, ys + 0x40, 15);
                    off += wv;
                    drawLine(c.img, xs + off, ys + 0x40, xs + off, ys + 0x30, 15);
                    drawLine(c.img, xs + off, ys + 0x30, xs + off + 4, ys + 0x30, 15);
                    off += 4;
                    drawLine(c.img, xs + off, ys + 0x40, xs + off, ys + 0x30, 15);
                }
                drawLine(c.img, xs + off, ys + 0x40, xs + 0x82, ys + 0x40, 15);
            });
        };
    }
    scripts_.emplace_back();
    ui_.out = &scripts_.back();
    trace.push_back("describe_new_info " + std::to_string(w_.hdr.hereOrg) + " " + std::to_string(w_.hdr.hereCity) +
                    " " + std::to_string(amount) + " 1");
    const int r = sim_->describeNewInfo(w_.hdr.hereOrg, w_.hdr.hereCity, amount, 1);
    trace.push_back("  -> " + std::to_string(r));
    ui_.out = nullptr;
    return static_cast<int>(scripts_.size()) - 1;
}

}  // namespace reaction
