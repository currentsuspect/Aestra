// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The export dialog's settings panel, built to canvas-2 board 3 (V82-P12): the choices in the order you make them.
// WHAT: the song, the loop or the selection, and one file or stems plus the master. WHERE: a name and a folder, with
// the full path underneath. FORMAT: compact chips (format, depth, rate, tail). Then one line worked out before you
// press Export: how long the file will be, roughly how big, and how many files.
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "AudioEngine.h"
#include "ExportDialog.h"
#include "NUIThemeSystem.h"
#include "TrackManager.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {
constexpr float kPad = 18.0f;
constexpr float kCaptionH = 18.0f;
constexpr float kSegH = 30.0f;
constexpr float kRowH = 30.0f;
constexpr float kChipH = 26.0f;

std::string formatLabel(Aestra::Audio::ExportFormat f) {
    switch (f) {
    case Aestra::Audio::ExportFormat::Flac:
        return "FLAC";
    case Aestra::Audio::ExportFormat::Mp3:
        return "MP3";
    default:
        return "WAV";
    }
}

std::vector<std::string> depthsFor(Aestra::Audio::ExportFormat f) {
    switch (f) {
    case Aestra::Audio::ExportFormat::Flac:
        return {"16-bit", "24-bit"};
    case Aestra::Audio::ExportFormat::Mp3:
        return {"320 kbps"};
    default:
        return {"16-bit", "24-bit", "32-bit float"};
    }
}
} // namespace

void ExportDialog::buildBoardControls() {
    // The rows of the old layout go: scope and stems are segmented controls painted here, the depth menu follows the
    // format, and the old labels are not needed.
    for (const auto& label : {m_outputLabel, m_scopeLabel, m_rateLabel, m_depthLabel, m_tailLabel})
        label->setVisible(false);
    m_scopeDropdown->setVisible(false);
    m_stemsCheckbox->setVisible(false);

    m_formats = {Aestra::Audio::ExportFormat::Wav};
    for (const auto& option : m_bitDepthOptions) {
        if (option.rfind("FLAC", 0) == 0 && m_formats.back() != Aestra::Audio::ExportFormat::Flac)
            m_formats.push_back(Aestra::Audio::ExportFormat::Flac);
        if (option.rfind("MP3", 0) == 0)
            m_formats.push_back(Aestra::Audio::ExportFormat::Mp3);
    }
    m_formatDropdown = std::make_shared<AestraUI::NUIDropdown>();
    for (size_t i = 0; i < m_formats.size(); ++i)
        m_formatDropdown->addItem(formatLabel(m_formats[i]), static_cast<int>(i));
    m_formatDropdown->setOnSelectionChanged([this](int i) {
        if (i >= 0 && static_cast<size_t>(i) < m_formats.size())
            setFormat(m_formats[static_cast<size_t>(i)], -1);
    });
    addChild(m_formatDropdown);
    m_depthDropdown->setOnSelectionChanged([this](int i) { setFormat(m_format, i); });
    m_browseButton->setText("in folder");
    m_outputInput->setOnTextChange([this](const std::string&) {
        refreshOutputPath();
        setDirty(true);
    });
}

// m_selectedBitDepth stays the index into m_bitDepthOptions ("WAV 24-bit", "FLAC 16-bit", "MP3 320 kbps"), so the
// saved export settings keep their meaning; the two chips are a view of it.
void ExportDialog::setFormat(Aestra::Audio::ExportFormat format, int depthChoice) {
    const bool formatChanged = format != m_format;
    m_format = format;
    const auto depths = depthsFor(format);
    if (depthChoice < 0 || static_cast<size_t>(depthChoice) >= depths.size()) {
        depthChoice = format == Aestra::Audio::ExportFormat::Mp3 ? 0 : 1; // 24-bit by default
    }
    const std::string wanted = formatLabel(format) + " " + depths[static_cast<size_t>(depthChoice)];
    for (size_t i = 0; i < m_bitDepthOptions.size(); ++i) {
        if (m_bitDepthOptions[i] == wanted)
            m_selectedBitDepth = static_cast<int>(i);
    }
    if (formatChanged)
        syncFormatChips();
    refreshOutputPath();
    setDirty(true);
}

void ExportDialog::syncFormatChips() {
    const std::string& option = m_bitDepthOptions[static_cast<size_t>(
        std::clamp(m_selectedBitDepth, 0, static_cast<int>(m_bitDepthOptions.size()) - 1))];
    m_format = formatOf(m_selectedBitDepth);
    for (size_t i = 0; i < m_formats.size(); ++i)
        if (m_formats[i] == m_format)
            m_formatDropdown->setSelectedIndex(static_cast<int>(i));
    const auto depths = depthsFor(m_format);
    m_depthDropdown->clearItems();
    int selected = 0;
    for (size_t i = 0; i < depths.size(); ++i) {
        m_depthDropdown->addItem(depths[i], static_cast<int>(i));
        if (option.find(depths[i]) != std::string::npos)
            selected = static_cast<int>(i);
    }
    m_depthDropdown->setSelectedIndex(selected);
}

namespace {
// The file name without any extension a format owns; "Untitled" when nothing is left (a new project's default
// export name is only the extension).
std::string bareName(std::string name) {
    for (const char* ext : {".wav", ".flac", ".mp3"}) {
        const size_t n = std::strlen(ext);
        if (name.size() >= n && name.compare(name.size() - n, n, ext) == 0)
            name.resize(name.size() - n);
    }
    return name.empty() ? std::string("Untitled") : name;
}

// Where an export goes when the project has no folder yet: Renders in the Aestra folder, else home.
std::string defaultExportFolder() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home)
        home = std::getenv("USERPROFILE");
#endif
    if (!home)
        return std::filesystem::current_path().string();
    std::error_code ec;
    const auto renders = std::filesystem::path(home) / "Documents" / "Aestra" / "Renders";
    return std::filesystem::is_directory(renders, ec) ? renders.string() : std::string(home);
}
} // namespace

void ExportDialog::setOutputFromPath(const std::string& path) {
    const std::filesystem::path p(path);
    m_outputFolder = p.parent_path().string();
    if (m_outputFolder.empty())
        m_outputFolder = defaultExportFolder();
    m_outputInput->setText(bareName(p.filename().string()));
    refreshOutputPath();
}

void ExportDialog::refreshOutputPath() {
    const std::string name = bareName(m_outputInput ? m_outputInput->getText() : std::string());
    m_outputPath =
        (std::filesystem::path(m_outputFolder) / (name + "." + Aestra::Audio::exportFormatExtension(m_format)))
            .string();
    if (m_browseButton) {
        const std::string folder = std::filesystem::path(m_outputFolder).filename().string();
        m_browseButton->setText("in " + (folder.empty() ? m_outputFolder : folder));
    }
}

std::string ExportDialog::summaryText() const {
    double seconds = -1.0;
    if (m_engine && m_trackManager) {
        const double bpm = m_engine->getBPM() > 0.0 ? m_engine->getBPM() : 120.0;
        double beats = -1.0;
        if (m_selectedScope == 0)
            beats = m_trackManager->getPlaylistModel().getTotalDurationBeats();
        else if (m_selectedScope == 1)
            beats = m_engine->getLoopEndBeat() - m_engine->getLoopStartBeat();
        if (beats > 0.0)
            seconds = beats * 60.0 / bpm + m_tailSeconds;
    }
    char length[32] = "\xe2\x80\x94"; // em dash: the selection's length is not known here
    if (seconds > 0.0) {
        const int s = static_cast<int>(std::lround(seconds));
        std::snprintf(length, sizeof(length), "%d:%02d", s / 60, s % 60);
    }
    std::string size = "\xe2\x80\x94";
    if (seconds > 0.0 && !m_stems) {
        const double rate = kSampleRates[std::clamp(m_selectedSampleRate, 0, 3)];
        const std::string& option = m_bitDepthOptions[static_cast<size_t>(
            std::clamp(m_selectedBitDepth, 0, static_cast<int>(m_bitDepthOptions.size()) - 1))];
        double bytes = 0.0;
        if (m_format == Aestra::Audio::ExportFormat::Mp3) {
            bytes = seconds * 320000.0 / 8.0;
        } else {
            const double sampleBytes = option.find("16") != std::string::npos   ? 2.0
                                       : option.find("32") != std::string::npos ? 4.0
                                                                                : 3.0;
            bytes = seconds * rate * 2.0 * sampleBytes;
            if (m_format == Aestra::Audio::ExportFormat::Flac)
                bytes *= 0.6; // typical for music
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%.0f MB", m_format == Aestra::Audio::ExportFormat::Flac ? "about " : "",
                      bytes / 1.0e6);
        size = buf;
    }
    const std::string files = m_stems ? "one WAV per track, plus the master" : "one stereo file";
    return std::string(length) + "  \xc2\xb7  " + size + "  \xc2\xb7  " + files;
}

void ExportDialog::layoutSettingsBoard() {
    const float x = m_dialogRect.x + kPad;
    const float w = m_dialogRect.width - kPad * 2.0f;
    float y = m_dialogRect.y + 32.0f + 14.0f;

    m_whatCaption = {x, y, w, kCaptionH};
    y += kCaptionH + 2.0f;
    const float segW3 = w / 3.0f;
    for (size_t i = 0; i < m_scopeSegments.size(); ++i)
        m_scopeSegments[i] = {x + segW3 * static_cast<float>(i), y, segW3, kSegH};
    y += kSegH + 6.0f;
    for (size_t i = 0; i < m_stemsSegments.size(); ++i)
        m_stemsSegments[i] = {x + w * 0.5f * static_cast<float>(i), y, w * 0.5f, kSegH};
    y += kSegH + 16.0f;

    m_whereCaption = {x, y, w, kCaptionH};
    y += kCaptionH + 2.0f;
    const float folderW = 120.0f;
    m_outputInput->setBounds({x, y, w - folderW - 6.0f, kRowH});
    m_browseButton->setBounds({x + w - folderW, y, folderW, kRowH});
    y += kRowH + 4.0f;
    m_pathLine = {x, y, w, 16.0f};
    y += 16.0f + 12.0f;

    m_formatCaption = {x, y, w, kCaptionH};
    y += kCaptionH + 2.0f;
    float cx = x;
    const auto chip = [&](const std::shared_ptr<AestraUI::NUIComponent>& c, float cw) {
        c->setBounds({cx, y, cw, kChipH});
        cx += cw + 6.0f;
    };
    chip(m_formatDropdown, 78.0f);
    chip(m_depthDropdown, 112.0f);
    chip(m_rateDropdown, 96.0f);
    cx += 34.0f; // room for the "Tail" word painted before the field
    chip(m_tailField, 56.0f);
    y += kChipH + 12.0f;
    m_summaryLine = {x, y, w, 22.0f};
}

void ExportDialog::paintSettingsBoard(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const float capFont = theme.getCurrentTheme().fontSizeXS;
    const auto muted = theme.getColor("textMuted");
    const auto border = theme.getColor("border");
    const auto caption = [&](const AestraUI::NUIRect& r, const char* text) {
        renderer.drawText(text, {r.x, renderer.calculateTextY(r, capFont)}, capFont, muted);
    };
    caption(m_whatCaption, "WHAT");
    caption(m_whereCaption, "WHERE");
    caption(m_formatCaption, "FORMAT");

    // A segmented control: one outline, the chosen segment filled.
    const auto segmented = [&](const auto& segments, const char* const* labels, int chosen) {
        const AestraUI::NUIRect all(segments.front().x, segments.front().y,
                                    segments.back().right() - segments.front().x, segments.front().height);
        renderer.strokeRoundedRect(all, 4.0f, 1.0f, border);
        for (size_t i = 0; i < segments.size(); ++i) {
            const auto& s = segments[i];
            const bool on = static_cast<int>(i) == chosen;
            if (on) {
                renderer.fillRoundedRect({s.x + 3.0f, s.y + 3.0f, s.width - 6.0f, s.height - 6.0f}, 3.0f,
                                         theme.getColor("primary").withAlpha(0.24f));
            }
            renderer.drawTextCentered(labels[i], s, 13.0f, theme.getColor(on ? "textPrimary" : "textSecondary"));
        }
    };
    static const char* const kScope[] = {"Full song", "Loop", "Selection"};
    static const char* const kStems[] = {"One file", "Stems + master"};
    segmented(m_scopeSegments, kScope, m_selectedScope);
    segmented(m_stemsSegments, kStems, m_stems ? 1 : 0);

    const std::string ext = std::string(".") + Aestra::Audio::exportFormatExtension(m_format);

    std::string where = m_outputPath;
    if (const char* home = std::getenv("HOME"); home && where.rfind(home, 0) == 0)
        where = "~" + where.substr(std::strlen(home));
    if (m_stems) {
        const std::filesystem::path base(m_outputPath);
        where = std::filesystem::path(where).parent_path().string() + "/" + base.stem().string() + " - 01 Track" + ext +
                ", ...";
    }
    renderer.drawText(where, {m_pathLine.x, renderer.calculateTextY(m_pathLine, 11.0f)}, 11.0f, muted);

    const auto& tail = m_tailField->getBounds();
    renderer.drawText("Tail", {tail.x - 32.0f, renderer.calculateTextY(tail, 12.5f)}, 12.5f,
                      theme.getColor("textSecondary"));

    renderer.drawLine({m_summaryLine.x, m_summaryLine.y - 4.0f}, {m_summaryLine.right(), m_summaryLine.y - 4.0f}, 1.0f,
                      border.withAlpha(0.6f));
    renderer.drawText(summaryText(), {m_summaryLine.x, renderer.calculateTextY(m_summaryLine, 11.5f)}, 11.5f, muted);
}

bool ExportDialog::handleBoardClick(const AestraUI::NUIMouseEvent& event) {
    if (!event.pressed || event.button != AestraUI::NUIMouseButton::Left)
        return false;
    for (size_t i = 0; i < m_scopeSegments.size(); ++i) {
        if (m_scopeSegments[i].contains(event.position)) {
            m_selectedScope = static_cast<int>(i);
            setDirty(true);
            return true;
        }
    }
    for (size_t i = 0; i < m_stemsSegments.size(); ++i) {
        if (m_stemsSegments[i].contains(event.position)) {
            m_stems = i == 1;
            // Stems are written as WAV (exportStems): show that rather than a format they will not get.
            if (m_stems && m_format != Aestra::Audio::ExportFormat::Wav)
                setFormat(Aestra::Audio::ExportFormat::Wav, -1);
            m_formatDropdown->setEnabled(!m_stems);
            refreshOutputPath();
            setDirty(true);
            return true;
        }
    }
    return false;
}

// Drawn after the controls: the extension sits inside the name field, so it must paint over it. It follows the format,
// so it is shown, not typed.
void ExportDialog::paintSettingsBoardOverlay(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& in = m_outputInput->getBounds();
    const std::string ext = std::string(".") + Aestra::Audio::exportFormatExtension(m_format);
    const float extW = renderer.measureText(ext, 12.5f).width;
    renderer.drawText(ext, {in.right() - 10.0f - extW, renderer.calculateTextY(in, 12.5f)}, 12.5f,
                      theme.getColor("textMuted"));
}
