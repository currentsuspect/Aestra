// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Graphics/NUIRenderer.h"
#include "NUITypes.h"

#include <cstdint>

namespace Aestra {

/// The generated cover: a quiet background with two or three flat shapes, always the same for the same seed.
inline void drawCover(AestraUI::NUIRenderer& r, const AestraUI::NUIRect& rc, uint32_t seed) {
    using namespace AestraUI;
    struct Palette { uint32_t bg, a, b; };
    static const Palette palettes[] = {{0xff083d1d, 0xff3ec46d, 0xff18b8a6}, {0xff1d1536, 0xff14b8c9, 0xff7a6bd6}, {0xff0a2036, 0xfff08a00, 0xffb08a00},
                                       {0xff2a1230, 0xffe86bb9, 0xffb2506f}, {0xff301020, 0xfff0a0a0, 0xffc9a100}, {0xff5b1b1b, 0xffc9a100, 0xff6c87c9}};
    const Palette& p = palettes[seed % (sizeof(palettes) / sizeof(palettes[0]))];
    const NUIColor bg = NUIColor::fromHex(p.bg), a = NUIColor::fromHex(p.a), b = NUIColor::fromHex(p.b);
    r.fillRect(rc, bg);
    const float s = rc.width;
    switch ((seed >> 8) % 3) {
        case 0:  // a circle and a square overlapping
            r.fillCircle({rc.x + s * 0.40f, rc.y + s * 0.42f}, s * 0.26f, a);
            r.fillRect({rc.x + s * 0.50f, rc.y + s * 0.46f, s * 0.30f, s * 0.30f}, b.withAlpha(0.85f));
            break;
        case 1:  // two bars across
            r.fillRect({rc.x, rc.y + s * 0.62f, s, s * 0.13f}, a);
            r.fillRect({rc.x, rc.y + s * 0.75f, s, s * 0.06f}, b.withAlpha(0.8f));
            break;
        default:  // a tall bar and a ring
            r.fillRect({rc.x + s * 0.30f, rc.y, s * 0.10f, s}, a);
            r.strokeCircle({rc.x + s * 0.68f, rc.y + s * 0.42f}, s * 0.17f, 2.0f, b);
            break;
    }
}


} // namespace Aestra
