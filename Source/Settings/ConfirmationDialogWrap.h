// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The confirmation dialog's message wrapping, header-only so it is testable without a window.

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace Aestra {

// A UTF-8 continuation byte (10xxxxxx): never the first byte of a character, so never a place to cut.
inline bool isContinuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Split `text` into paragraphs on \n, then greedily word-wrap each to `maxWidth`.
// No renderer needed: approximate char width (matches renderer fallback fontSize*0.5).
inline std::vector<std::string> wrapMessageLines(const std::string& text, float maxWidth, float fontSize) {
    std::vector<std::string> lines;
    const float charWidth = fontSize * 0.5f;
    if (maxWidth <= 0.0f || charWidth <= 0.0f) { lines.push_back(text); return lines; }
    const size_t maxChars = static_cast<size_t>(std::max(1.0f, maxWidth / charWidth));
    size_t start = 0;
    while (true) {
        const size_t nl = text.find('\n', start);
        const std::string paragraph = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string current;
        size_t wordStart = 0;
        while (wordStart <= paragraph.size()) {
            const size_t sp = paragraph.find(' ', wordStart);
            const std::string word = paragraph.substr(wordStart, sp == std::string::npos ? std::string::npos : sp - wordStart);
            if (word.size() > maxChars) {
                // One token longer than a line (a path has no spaces): split it into full-width fragments, on
                // character boundaries, so none is drawn past the edge and none of it is lost. The tail stays in
                // `current` so a following word can still join it.
                if (!current.empty()) { lines.push_back(current); current.clear(); }
                size_t pos = 0;
                while (word.size() - pos > maxChars) {
                    size_t cut = pos + maxChars;
                    while (cut > pos && isContinuationByte(word[cut])) --cut; // back off to a character boundary
                    if (cut == pos) {                                         // a character wider than the line: take it whole
                        cut = pos + 1;
                        while (cut < word.size() && isContinuationByte(word[cut])) ++cut;
                    }
                    lines.push_back(word.substr(pos, cut - pos));
                    pos = cut;
                }
                current = word.substr(pos);
            } else {
                const std::string trial = current.empty() ? word : current + " " + word;
                if (!current.empty() && trial.size() > maxChars) { lines.push_back(current); current = word; }
                else { current = trial; }
            }
            if (sp == std::string::npos) break;
            wordStart = sp + 1;
        }
        lines.push_back(current);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

} // namespace Aestra
