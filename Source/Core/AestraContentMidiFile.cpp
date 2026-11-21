// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// File > Import MIDI File... and Export Selected Pattern as MIDI...: the app side of Midi/StandardMidiFile.h.
// The parsing, the mapping to patterns and the undo command are pure and tested on their own
// (MidiPatternBridgeTest); this file only reads and writes the file, picks the unit, and tells the user.
// Kept out of AestraContent.cpp, which is at its size pin.
#include "AestraContent.h"

#include "../AestraCore/include/AestraFile.h"
#include "../AestraCore/include/AestraLog.h"
#include "Commands/ImportMidiPatternsCommand.h"
#include "Core/AudioEngine.h"
#include "Midi/MidiPatternBridge.h"
#include "TrackManager.h"
#include "../Panels/ArsenalPanel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr uintmax_t kMaxMidiFileBytes = 64u * 1024u * 1024u;

// Write to "<path>.tmp", flush it to disk, then rename over the target, so a failed export never leaves half a file
// where the user's old one was.
bool writeBytesAtomic(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    const std::string tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!Aestra::syncOfstream(out, tmp)) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string bpmText(double bpm) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), std::fabs(bpm - std::round(bpm)) < 0.005 ? "%.0f" : "%.2f", bpm);
    return buf;
}

} // namespace

bool AestraContent::importMidiFile(const std::string& path) {
    if (!m_trackManager) return false;
    const std::filesystem::path fsPath(path);
    std::error_code ec;
    const auto size = std::filesystem::file_size(fsPath, ec);
    if (ec) {
        showToast("Could not open " + fsPath.filename().string());
        return false;
    }
    if (size > kMaxMidiFileBytes) {
        showToast(fsPath.filename().string() + " is too large to be a MIDI file");
        return false;
    }
    std::vector<uint8_t> bytes;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            showToast("Could not open " + fsPath.filename().string());
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto parsed = Midi::readStandardMidiFile(bytes);
    if (!parsed.ok) {
        AESTRA_LOG_WARNING("MIDI import failed for " + path + ": " + parsed.error);
        showToast("Could not import " + fsPath.filename().string() + ": " + parsed.error);
        return false;
    }

    // Notes go to the unit being edited (the active pattern's, else the selected one), else the first row.
    auto& units = m_trackManager->getUnitManager();
    UnitID unit = resolveEditingUnitForPattern(getActivePatternID());
    if (!units.getUnit(unit)) {
        const auto all = units.getAllUnitIDs();
        unit = all.empty() ? 0 : all.front();
    }

    auto plan = Midi::planPatternImport(parsed.data, fsPath.stem().string(), unit);
    if (plan.empty()) {
        showToast(fsPath.filename().string() + " has no notes to import");
        return false;
    }

    // "Empty" = no note in any pattern and nothing on the timeline. Only then may the file's tempo replace the project's.
    auto& patterns = m_trackManager->getPatternManager();
    bool empty = true;
    patterns.forEachPattern([&](const PatternSource& p) {
        if (p.isMidi() && !p.getMidiNotes().empty()) empty = false;
        if (p.isAudio()) empty = false;
    });
    auto& playlist = m_trackManager->getPlaylistModel();
    for (const auto& laneId : playlist.getLaneIDs()) {
        if (const auto* lane = playlist.getLane(laneId); lane && !lane->clips.empty()) empty = false;
    }

    const size_t count = plan.size();
    auto cmd = std::make_shared<ImportMidiPatternsCommand>(patterns, std::move(plan));
    m_trackManager->getCommandHistory().pushAndExecute(cmd);
    if (cmd->createdIds().empty()) return false;
    refreshAfterHistoryChange();

    std::string message = "Imported " + std::to_string(count) + (count == 1 ? " pattern from " : " patterns from ") +
                          fsPath.filename().string();
    if (!parsed.data.tempos.empty()) {
        const double fileBpm = parsed.data.tempos.front().bpm;
        const double projectBpm = m_audioEngine ? static_cast<double>(m_audioEngine->getBPM())
                                                : m_trackManager->getPlaylistModel().getBPM();
        if (empty && fileBpm >= 20.0 && fileBpm <= 999.0) {
            if (m_audioEngine) m_audioEngine->setBPM(static_cast<float>(fileBpm));
            setPluginTempo(static_cast<float>(fileBpm));
            m_trackManager->getPlaylistModel().setBPM(fileBpm);
            m_trackManager->getTimelineClock().setTempo(fileBpm);
            message += ". Tempo set to " + bpmText(fileBpm) + " BPM from the file";
        } else if (std::fabs(fileBpm - projectBpm) >= 0.5) {
            message += ". The file is " + bpmText(fileBpm) + " BPM; the project stays at " + bpmText(projectBpm);
        }
    }
    if (unit == 0) message += ". Add a row to the Arsenal to hear the notes";
    showToast(message);
    return true;
}

bool AestraContent::exportActivePatternAsMidi(const std::string& path) {
    if (!m_trackManager || path.empty()) return false;
    const auto id = getActivePatternID();
    const auto* pattern = id.isValid() ? m_trackManager->getPatternManager().getPattern(id) : nullptr;
    if (!pattern || !pattern->isMidi()) {
        showToast("Select a pattern with notes to export");
        return false;
    }
    const double bpm = m_audioEngine ? static_cast<double>(m_audioEngine->getBPM())
                                     : m_trackManager->getPlaylistModel().getBPM();
    const auto& units = m_trackManager->getUnitManager();
    const auto smf = Midi::buildPatternExport(pattern->name, pattern->getMidiNotes(), bpm, [&](uint64_t u) {
        const auto* info = units.getUnit(u);
        return info ? info->name : std::string();
    });
    std::filesystem::path out(path);
    if (out.extension().empty()) out.replace_extension(".mid");
    if (!writeBytesAtomic(out, Midi::writeStandardMidiFile(smf))) {
        showToast("Could not write " + out.filename().string());
        return false;
    }
    showToast("Exported " + pattern->name + " to " + out.filename().string());
    return true;
}
