// Character portraits built from face parts (covert-decomp GAMELIB/FACE.C
// init_face_parts, draw_face, draw_face_parts, draw_portrait) and the leaders'
// faces of FINAL (NEWCHAR.C face_defs, face_code_for, draw_face_for).
//
// The programs keep the parts as driver images in one array the code calls
// icons[] (draw_sprite_a(n) draws icons[n]); draw_face grabs them from
// FACESF.PIC and FACES.PIC on page 2, in the order of the arrays in GAMELIB.H:
//   00h-27h faces_f: female heads, 30x33 from the 32x35 grid of FACESF.PIC
//           (row 0 hair, 1 eyes, 2 nose, 3 mouth, 4 the head itself)
//   28h-4Fh faces_m: the same from FACES.PIC
//   50h-5Fh face_part_imgs: 8x8 overlays (init_face_parts, from FACES.PIC)
//   60h-66h face_hair      68h-6Bh face_extra_m (44x18 shoulders)
//   6Ch-6Fh face_img_a..d (6Dh, 6Eh: the corners of the page frames' title tab)
//   70h-73h face_extra_f
// Colour 0 is transparent when an image is drawn (MGRAPHIC draw_image).
//
// A face code (Person.id): bit 0 male, bit 1 the head variant, bits 2-3 the skin
// tone, then 3 bits each for the mouth (4), nose (7), eyes (10) and hair (13).
#pragma once

#include <array>
#include <cstdint>

#include "core/GameData.h"
#include "core/Image.h"

namespace reaction {

// face_defs: per leader code, sex (1 female), head variant, hair, eyes, nose,
// mouth (1-based), skin tone, and the shoulders image.
extern const std::array<std::array<int, 8>, 26> kFaceDefs;

// face_code_for(leader): the face code of an organization leader.
int faceCodeFor(int leader);

class FaceSprites {
public:
    static constexpr int kCount = 0x74;

    // init_face_parts (draw_face + the face_part_imgs grab).
    static FaceSprites load(const GameData& data);

    const Image& sprite(int n) const { return sprites_[static_cast<std::size_t>(n)]; }
    // draw_sprite_a: icons[n] at (x, y), colour 0 transparent.
    void draw(Image& dst, int n, int x, int y) const;

private:
    std::array<Image, kCount> sprites_{};
};

// draw_face_parts: the head and features of `face` at (x, y) (32x35), then the
// skin tone (recolouring 11 and 12).
void drawFaceParts(Image& dst, const FaceSprites& s, int face, int x, int y);

// draw_portrait (GAMELIB): a framed portrait (54x56 from (x - 2, y - 2)).
void drawPortrait(Image& dst, const FaceSprites& s, int face, int x, int y);

// draw_face_for (FINAL): a leader's framed portrait, as the face line-up and the
// career summary show it.
void drawFaceFor(Image& dst, const FaceSprites& s, int leader, int x, int y);

}  // namespace reaction
