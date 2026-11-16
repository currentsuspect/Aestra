// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Fallback faces for glyphs the UI face lacks, tried in order: CJK, then symbols (dingbats and arrows, which sample
// packs put in file names, e.g. U+2731). A missing glyph measures 0 but draws a gap, so a name spills into whatever
// column comes after it; a fallback face keeps measuring and drawing in agreement.

#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <vector>

namespace AestraUI {

inline const std::vector<std::string>& cjkFallbackFacePaths() {
    static const std::vector<std::string> paths = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",             "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
    };
    return paths;
}

inline const std::vector<std::string>& symbolFallbackFacePaths() {
    static const std::vector<std::string> paths = {
        "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansSymbols2-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/System/Library/Fonts/Apple Symbols.ttf",
        "C:/Windows/Fonts/seguisym.ttf",
    };
    return paths;
}

/// The first face in @p paths that FreeType opens, or nullptr; @p loadedPath names it.
inline FT_Face openFirstFace(FT_Library library, const std::vector<std::string>& paths, std::string& loadedPath) {
    for (const auto& path : paths) {
        FT_Face face = nullptr;
        if (FT_New_Face(library, path.c_str(), 0, &face) == 0) { loadedPath = path; return face; }
    }
    return nullptr;
}

} // namespace AestraUI
