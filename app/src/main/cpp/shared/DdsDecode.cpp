// DDS layout: Microsoft "DDS_HEADER" / "DDS_PIXELFORMAT" (and DDS_HEADER_DXT10);
// block formats as in the S3TC / BC1-BC3 specifications.
#include "DdsDecode.h"

#include <algorithm>
#include <cstring>

namespace WorldRPG::Script {
namespace {

constexpr uint32_t kPixelFourCC = 0x4;
constexpr uint32_t kPixelRgb = 0x40;
constexpr uint32_t kPixelAlpha = 0x1;
constexpr unsigned kMaxSide = 8192;

uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void Rgb565(uint16_t c, int& r, int& g, int& b) {
    r = (c >> 11) & 31;
    g = (c >> 5) & 63;
    b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
}

// The four colours of a BC1 colour block. colourOnly (BC2/BC3) always uses the four-colour mode.
void Palette(const uint8_t* block, bool colourOnly, uint32_t out[4]) {
    const uint16_t c0 = Le16(block), c1 = Le16(block + 2);
    int r[4], g[4], b[4];
    Rgb565(c0, r[0], g[0], b[0]);
    Rgb565(c1, r[1], g[1], b[1]);
    uint32_t alpha3 = 0xFF000000u;
    if (c0 > c1 || colourOnly) {
        r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
        r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
    } else {
        r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
        r[3] = g[3] = b[3] = 0;
        alpha3 = 0;  // BC1 1-bit alpha: index 3 is transparent black
    }
    for (int i = 0; i < 4; ++i)
        out[i] = (i == 3 ? alpha3 : 0xFF000000u) | (uint32_t(r[i]) << 16) | (uint32_t(g[i]) << 8) | uint32_t(b[i]);
}

void DecodeBlocks(const uint8_t* data, unsigned w, unsigned h, int kind, std::vector<uint32_t>& argb) {
    // kind 1 BC1 (8-byte blocks), 2 BC2, 3 BC3 (16-byte blocks: alpha then colour).
    const size_t blockBytes = kind == 1 ? 8 : 16;
    const unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (unsigned by = 0; by < bh; ++by) {
        for (unsigned bx = 0; bx < bw; ++bx) {
            const uint8_t* block = data + (static_cast<size_t>(by) * bw + bx) * blockBytes;
            const uint8_t* colour = kind == 1 ? block : block + 8;
            uint32_t palette[4];
            Palette(colour, kind != 1, palette);
            uint8_t alpha[16];
            if (kind == 2) {
                for (int i = 0; i < 16; ++i) {
                    const int nibble = (block[i / 2] >> ((i & 1) * 4)) & 0xF;
                    alpha[i] = static_cast<uint8_t>(nibble * 17);
                }
            } else if (kind == 3) {
                uint8_t a[8];
                a[0] = block[0];
                a[1] = block[1];
                if (a[0] > a[1]) {
                    for (int i = 1; i < 7; ++i) a[i + 1] = static_cast<uint8_t>(((7 - i) * a[0] + i * a[1]) / 7);
                } else {
                    for (int i = 1; i < 5; ++i) a[i + 1] = static_cast<uint8_t>(((5 - i) * a[0] + i * a[1]) / 5);
                    a[6] = 0;
                    a[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) bits |= uint64_t(block[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i) alpha[i] = a[(bits >> (3 * i)) & 7];
            }
            const uint32_t indices = Le32(colour + 4);
            for (int py = 0; py < 4; ++py) {
                const unsigned y = by * 4 + py;
                if (y >= h) break;
                for (int px = 0; px < 4; ++px) {
                    const unsigned x = bx * 4 + px;
                    if (x >= w) continue;
                    const int i = py * 4 + px;
                    uint32_t pixel = palette[(indices >> (2 * i)) & 3];
                    if (kind != 1) pixel = (pixel & 0x00FFFFFFu) | (uint32_t(alpha[i]) << 24);
                    argb[static_cast<size_t>(y) * w + x] = pixel;
                }
            }
        }
    }
}

int Shift(uint32_t mask) {
    if (!mask) return 0;
    int s = 0;
    while (!(mask & 1)) {
        mask >>= 1;
        ++s;
    }
    return s;
}

uint32_t Channel(uint32_t value, uint32_t mask) {
    if (!mask) return 0;
    const int s = Shift(mask);
    const uint32_t max = mask >> s;
    return ((value & mask) >> s) * 255 / max;
}

}  // namespace

bool DecodeDds(const uint8_t* data, size_t size, std::vector<uint32_t>& argb, unsigned& width, unsigned& height) {
    if (!data || size < 128 || std::memcmp(data, "DDS ", 4) != 0 || Le32(data + 4) != 124) return false;
    const unsigned h = Le32(data + 12), w = Le32(data + 16);
    if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide) return false;
    const uint8_t* pf = data + 76;
    const uint32_t flags = Le32(pf + 4);
    size_t offset = 128;
    int kind = 0;  // 1-3 block formats, 4 uncompressed
    bool premultiplied = false;  // DXT2 / DXT4 store colour premultiplied by alpha
    if (flags & kPixelFourCC) {
        const uint8_t* cc = pf + 8;
        premultiplied = std::memcmp(cc, "DXT2", 4) == 0 || std::memcmp(cc, "DXT4", 4) == 0;
        if (std::memcmp(cc, "DXT1", 4) == 0) kind = 1;
        else if (std::memcmp(cc, "DXT2", 4) == 0 || std::memcmp(cc, "DXT3", 4) == 0) kind = 2;
        else if (std::memcmp(cc, "DXT4", 4) == 0 || std::memcmp(cc, "DXT5", 4) == 0) kind = 3;
        else if (std::memcmp(cc, "DX10", 4) == 0) {
            if (size < 148) return false;
            offset = 148;
            switch (Le32(data + 128)) {  // DXGI_FORMAT
                case 71: case 72: kind = 1; break;  // BC1_UNORM(_SRGB)
                case 74: case 75: kind = 2; break;  // BC2
                case 77: case 78: kind = 3; break;  // BC3
                default: return false;
            }
        } else {
            return false;
        }
    } else if (flags & kPixelRgb) {
        kind = 4;
    } else {
        return false;
    }
    argb.assign(static_cast<size_t>(w) * h, 0);
    if (kind <= 3) {
        const size_t blocks = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4);
        if (size - offset < blocks * (kind == 1 ? 8 : 16)) return false;
        DecodeBlocks(data + offset, w, h, kind, argb);
        if (premultiplied) {
            for (uint32_t& pixel : argb) {
                const uint32_t a = pixel >> 24;
                if (a == 0 || a == 255) continue;
                auto un = [a](uint32_t c) { return std::min<uint32_t>(255, c * 255 / a); };
                pixel = (a << 24) | (un((pixel >> 16) & 255) << 16) | (un((pixel >> 8) & 255) << 8) | un(pixel & 255);
            }
        }
    } else {
        const uint32_t bits = Le32(pf + 12);
        if (bits != 24 && bits != 32) return false;
        const uint32_t rm = Le32(pf + 16), gm = Le32(pf + 20), bm = Le32(pf + 24);
        const uint32_t am = (flags & kPixelAlpha) ? Le32(pf + 28) : 0;
        const size_t bpp = bits / 8;
        if (size - offset < static_cast<size_t>(w) * h * bpp) return false;
        const uint8_t* p = data + offset;
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i, p += bpp) {
            const uint32_t v = bpp == 4 ? Le32(p) : (p[0] | (p[1] << 8) | (uint32_t(p[2]) << 16));
            const uint32_t a = am ? Channel(v, am) : 255;
            argb[i] = (a << 24) | (Channel(v, rm) << 16) | (Channel(v, gm) << 8) | Channel(v, bm);
        }
    }
    width = w;
    height = h;
    return true;
}

}  // namespace WorldRPG::Script
