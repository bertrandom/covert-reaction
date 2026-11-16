#include "game/Face.h"

#include "formats/Pic.h"
#include "formats/TextWindow.h"

namespace redux {

const std::array<std::array<int, 8>, 26> kFaceDefs = {{
    {0, 1, 2, 2, 2, 2, 3, 1}, {0, 0, 3, 3, 3, 3, 1, 2}, {0, 1, 5, 3, 4, 6, 0, 3},
    {0, 0, 8, 4, 6, 1, 3, 1}, {0, 1, 5, 5, 5, 5, 0, 0}, {0, 1, 6, 6, 6, 6, 3, 2},
    {0, 1, 4, 8, 5, 2, 3, 1}, {0, 1, 7, 7, 8, 7, 1, 3}, {0, 1, 1, 2, 3, 7, 2, 1},
    {0, 1, 3, 1, 8, 5, 3, 3}, {0, 1, 2, 2, 2, 3, 3, 0}, {0, 0, 3, 3, 3, 1, 2, 1},
    {0, 1, 4, 8, 5, 3, 3, 2}, {0, 0, 8, 4, 6, 5, 3, 3}, {0, 1, 7, 7, 8, 8, 0, 0},
    {0, 1, 6, 6, 6, 8, 3, 3}, {1, 0, 2, 2, 2, 2, 3, 2}, {1, 0, 4, 4, 4, 4, 1, 3},
    {1, 0, 7, 1, 8, 5, 0, 3}, {1, 0, 8, 5, 3, 7, 3, 3}, {1, 1, 1, 8, 4, 2, 1, 2},
    {1, 1, 4, 7, 2, 1, 0, 2}, {1, 1, 5, 2, 6, 3, 0, 0}, {1, 1, 5, 5, 5, 5, 3, 0},
    {1, 0, 5, 3, 1, 6, 3, 1}, {1, 0, 2, 6, 7, 8, 0, 0},
}};

int faceCodeFor(int p) {
    const auto& f = kFaceDefs[static_cast<std::size_t>(p)];
    int code = f[1] * 2 + f[6] * 4 - f[0] + 1;
    for (int i = 0; i < 4; ++i) code += (f[static_cast<std::size_t>(i) + 2] - 1) << (i * 3 + 4);
    return code;
}

namespace {

// drv_get_image: a w x h block of a page.
Image grab(const Image& page, int x, int y, int w, int h) {
    Image img(w, h, 0);
    img.blit(page, x, y, w, h, 0, 0);
    return img;
}

}  // namespace

FaceSprites FaceSprites::load(const GameData& data) {
    FaceSprites s;
    Image page(320, 200, 0);  // page 2: load_picture writes the picture's rows over it
    auto load = [&](const char* name) {
        auto pic = decodePicture(data.read(name));
        page.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
    };
    auto& sp = s.sprites_;
    load("facesf.pic");
    for (int i = 0; i < 40; ++i) sp[static_cast<std::size_t>(i)] = grab(page, i % 8 * 32 + 1, i / 8 * 35 + 1, 30, 33);
    for (int i = 0; i < 4; ++i) sp[static_cast<std::size_t>(0x70 + i)] = grab(page, 257, i * 20, 44, 18);
    load("faces.pic");
    for (int i = 0; i < 40; ++i)
        sp[static_cast<std::size_t>(0x28 + i)] = grab(page, i % 8 * 32 + 1, i / 8 * 35 + 1, 30, 33);
    for (int i = 0; i < 7; ++i) sp[static_cast<std::size_t>(0x60 + i)] = grab(page, i * 32 + 96, 174, 32, 26);
    for (int i = 0; i < 4; ++i) sp[static_cast<std::size_t>(0x68 + i)] = grab(page, 257, i * 20, 44, 18);
    sp[0x6C] = grab(page, 73, 175, 21, 22);
    sp[0x6D] = grab(page, 307, 0, 11, 8);
    sp[0x6E] = grab(page, 307, 9, 12, 8);
    sp[0x6F] = grab(page, 257, 160, 26, 14);
    // init_face_parts, after draw_face (FACES.PIC still on page 2).
    for (int i = 0; i < 16; ++i)
        sp[static_cast<std::size_t>(0x50 + i)] = grab(page, i % 8 * 9, i / 8 * 9 + 175, 8, 8);
    return s;
}

void FaceSprites::draw(Image& dst, int n, int x, int y) const {
    if (n < 0 || n >= kCount) return;
    const Image& img = sprite(n);
    dst.blit(img, 0, 0, img.width(), img.height(), x, y, 0);
}

void drawFaceParts(Image& dst, const FaceSprites& s, int face, int x, int y) {
    const int base = (face & 1) ? 0x28 : 0;  // faces_m : faces_f
    s.draw(dst, (face & 2) * 2 + base + 0x20, x + 1, y + 1);
    s.draw(dst, (face >> 4 & 7) + base + 0x18, x + 1, y + 1);
    s.draw(dst, (face >> 7 & 7) + base + 0x10, x + 1, y + 1);
    s.draw(dst, (face >> 10 & 7) + base + 0x08, x + 1, y + 1);
    s.draw(dst, (face >> 13 & 7) + base, x + 1, y + 1);
    switch (face >> 2 & 3) {  // video_mode != 'C'
    case 0: recolorRect(dst, x, y, 32, 35, 11, 14); break;
    case 1: recolorRect(dst, x, y, 32, 35, 11, 6); break;
    case 3: recolorRect(dst, x, y, 32, 35, 12, 6); [[fallthrough]];
    case 2: recolorRect(dst, x, y, 32, 35, 11, 5); break;
    }
}

void drawPortrait(Image& dst, const FaceSprites& s, int face, int x, int y) {
    fillRect(dst, x - 2, y - 2, 54, 56, 7);
    fillRect(dst, x - 1, y - 1, 52, 54, 15);
    fillRect(dst, x + 2, y + 2, 46, 45, 0);
    fillRect(dst, x + 3, y + 3, 44, 43, (face >> 2 & 3) == 3 ? 7 : 3);
    drawFaceParts(dst, s, face, x + 13, y + 3);
    static constexpr std::uint8_t kRecolor[][2] = {{8, 5}, {12, 7}, {14, 15}, {6, 8},
                                                   {4, 8}, {1, 8},  {10, 7},  {9, 7}};
    for (const auto& r : kRecolor) recolorRect(dst, x, y, 50, 50, r[0], r[1]);
    s.draw(dst, ((face & 1) ? 0x68 : 0x70) + (face & 3), x + 3, y + 28);
}

void drawFaceFor(Image& dst, const FaceSprites& s, int p, int x, int y) {
    const int code = faceCodeFor(p);
    fillRect(dst, x - 2, y - 2, 54, 56, 7);
    fillRect(dst, x - 1, y - 1, 52, 54, 13);
    fillRect(dst, x + 2, y + 2, 46, 45, 5);
    fillRect(dst, x + 3, y + 3, 44, 43, (code >> 2 & 3) == 3 ? 7 : 3);
    drawFaceParts(dst, s, code, x + 12, y + 3);
    s.draw(dst, kFaceDefs[static_cast<std::size_t>(p)][7] + ((code & 1) ? 0x68 : 0x70), x + 3, y + 28);
}

}  // namespace redux
