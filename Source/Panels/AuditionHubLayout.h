// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Aestra {

/**
 * @brief Where everything in the Audition hub sits (FD-26, V81-A): the navigation at the left, the player bar along
 * the bottom, and the page between them with its shelves of tiles. Pure geometry, so the hit-testing is tested
 * without a window.
 */
struct AuditionHubLayout {
    enum class Page { Home, Project, References, Queue, Search, Item };  // Item is a page of one thing, reached from a tile; it has no nav entry
    static constexpr int kPages = 5;  ///< pages in the navigation

    static constexpr float kNavW = 232.0f;
    static constexpr float kPlayerH = 88.0f;
    static constexpr float kPad = 40.0f;
    static constexpr float kGap = 20.0f;
    static constexpr float kMinTile = 150.0f;
    static constexpr float kTargetTile = 172.0f;
    static constexpr float kCaptionH = 52.0f;
    static constexpr float kShelfHeaderH = 56.0f;

    struct Frame {
        AestraUI::NUIRect nav, content, player;
        std::array<AestraUI::NUIRect, kPages> navItems;
        AestraUI::NUIRect cover, transportPrev, transportPlay, transportNext, seek;
        AestraUI::NUIRect toggleA, toggleB, listenOn, loudness;
    };

    static Frame frame(const AestraUI::NUIRect& b) {
        Frame f;
        f.nav = {b.x, b.y, kNavW, b.height - kPlayerH};
        f.player = {b.x, b.bottom() - kPlayerH, b.width, kPlayerH};
        f.content = {b.x + kNavW, b.y, b.width - kNavW, b.height - kPlayerH};
        for (int i = 0; i < kPages; ++i) f.navItems[static_cast<size_t>(i)] = {b.x + 12.0f, b.y + 66.0f + 38.0f * static_cast<float>(i), kNavW - 24.0f, 36.0f};
        f.cover = {b.x + 20.0f, f.player.y + 16.0f, 56.0f, 56.0f};
        const float cx = b.x + b.width * 0.5f;
        f.transportPrev = {cx - 20.0f - 52.0f, f.player.y + 8.0f, 36.0f, 36.0f};
        f.transportPlay = {cx - 20.0f, f.player.y + 4.0f, 40.0f, 40.0f};
        f.transportNext = {cx + 20.0f + 16.0f, f.player.y + 8.0f, 36.0f, 36.0f};
        f.seek = {cx - 320.0f, f.player.y + 62.0f, 640.0f, 14.0f};
        const float right = b.right() - 20.0f;
        f.loudness = {right - 64.0f, f.player.y + 30.0f, 64.0f, 28.0f};
        f.listenOn = {f.loudness.x - 12.0f - 112.0f, f.player.y + 28.0f, 112.0f, 32.0f};
        f.toggleB = {f.listenOn.x - 12.0f - 70.0f, f.player.y + 28.0f, 70.0f, 32.0f};
        f.toggleA = {f.toggleB.x - 82.0f, f.player.y + 28.0f, 82.0f, 32.0f};
        return f;
    }

    /// The page of one mix or reference: a hero with its cover and the compare control, the waveform of both sides, the
    /// numbers beside it, and (with a reference chosen) a rail at the right for the level match.
    struct ItemFrame {
        AestraUI::NUIRect hero, cover, play, compare, rail, body, wave, sections, actions, table;
    };
    static constexpr float kRailW = 282.0f;
    static ItemFrame itemFrame(const AestraUI::NUIRect& content, bool withRail) {
        ItemFrame f;
        const float railW = withRail ? kRailW : 0.0f;
        const float mainW = content.width - railW;
        f.hero = {content.x, content.y, mainW, 232.0f};
        f.cover = {content.x + kPad, content.y + 32.0f, 168.0f, 168.0f};
        const float tx = f.cover.right() + 32.0f;
        f.play = {tx, f.hero.bottom() - 76.0f, 56.0f, 56.0f};
        f.compare = {f.play.right() + 14.0f, f.hero.bottom() - 70.0f, 280.0f, 44.0f};
        f.rail = {content.right() - railW, content.y, railW, content.height};
        f.body = {content.x, f.hero.bottom(), mainW, std::max(0.0f, content.height - f.hero.height)};
        const float waveW = std::max(240.0f, (f.body.width - kPad * 2.0f) * 0.58f);
        f.wave = {content.x + kPad, f.body.y + 56.0f, waveW, 140.0f};
        f.sections = {f.wave.x, f.wave.bottom() + 6.0f, waveW, 22.0f};
        f.actions = {f.wave.x, f.sections.bottom() + 20.0f, waveW, 32.0f};
        f.table = {f.wave.right() + 36.0f, f.body.y + 40.0f, std::max(120.0f, f.body.right() - kPad - (f.wave.right() + 36.0f)), 280.0f};
        return f;
    }

    static int columns(float width) {
        const float usable = width - kPad * 2.0f;
        return std::max(1, static_cast<int>(std::floor((usable + kGap) / (kMinTile + kGap))));
    }
    static float tileSize(float width) {
        const int cols = columns(width);
        const float usable = width - kPad * 2.0f;
        return std::min(kTargetTile * 1.25f, (usable - kGap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
    }

    struct Shelf {
        AestraUI::NUIRect header;
        std::vector<AestraUI::NUIRect> tiles;  ///< the tile including its caption
        AestraUI::NUIRect cover(size_t i) const { const auto& t = tiles[i]; return {t.x, t.y, t.width, t.width}; }
    };

    /// Shelves stacked from @p top, wrapping tiles into rows. A shelf with no tiles still gets its header.
    static std::vector<Shelf> shelves(const AestraUI::NUIRect& content, float top, const std::vector<size_t>& counts) {
        std::vector<Shelf> out;
        const int cols = columns(content.width);
        const float ts = tileSize(content.width);
        float y = top;
        for (const size_t n : counts) {
            Shelf s;
            s.header = {content.x + kPad, y, content.width - kPad * 2.0f, kShelfHeaderH};
            y += kShelfHeaderH;
            for (size_t i = 0; i < n; ++i) {
                const int row = static_cast<int>(i) / cols, col = static_cast<int>(i) % cols;
                s.tiles.push_back({content.x + kPad + static_cast<float>(col) * (ts + kGap), y + static_cast<float>(row) * (ts + kCaptionH + kGap * 0.5f), ts, ts + kCaptionH});
            }
            const int rows = n ? (static_cast<int>(n) + cols - 1) / cols : 0;
            y += static_cast<float>(rows) * (ts + kCaptionH + kGap * 0.5f) + 24.0f;
            out.push_back(std::move(s));
        }
        return out;
    }

    static float contentHeight(const std::vector<Shelf>& shelves, float top) {
        if (shelves.empty()) return top;
        const auto& s = shelves.back();
        const float bottom = s.tiles.empty() ? s.header.bottom() : s.tiles.back().bottom();
        return bottom + 32.0f - top;
    }
};

} // namespace Aestra
