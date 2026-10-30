// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-M3 (FD-53): the piano roll through Muse. Notes in bulk, a note's length, deleting a range, and the FL-style
// duplicate of FD-52. Each verb is one step in the same undo history the editor uses. Its own file because
// CommandRegistry.cpp is close to the file-size limit.

#include "CommandRegistryFlags.h"
#include "Commands/CommandRegistry.h"
#include "Commands/ReplacePatternNotesCommand.h"
#include "Commands/ResizeNoteCommand.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

namespace {
using namespace RegistryFlags;
using Flags = std::unordered_map<std::string, std::string>;

constexpr size_t kMaxBatchNotes = 4096;

// A note belongs to a range when it STARTS inside it, as on the piano-roll ruler (PianoRollDuplicate::startsIn).
bool startsIn(const MidiNote& n, double from, double to) {
    return n.startBeat >= from - 1e-9 && n.startBeat < to - 1e-9;
}

std::string beats(double b) {
    std::string s = std::to_string(b);
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\n");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t\n") - first + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t begin = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(trim(s.substr(begin, i - begin)));
            begin = i + 1;
        }
    }
    return out;
}

// The MIDI pattern a verb names, or null with the reason recorded.
const PatternSource* midiPattern(TrackManager& tm, const Flags& flags, PatternID& id) {
    auto raw = requireFlag(flags, "pattern");
    auto value = raw ? safeStoull(*raw) : std::nullopt;
    if (!value)
        return nullptr;
    id = PatternID{*value};
    const PatternSource* pattern = tm.getPatternManager().getPattern(id);
    if (!pattern || !pattern->isMidi()) {
        CommandRegistry::fail("no such MIDI pattern: " + std::string(*raw));
        return nullptr;
    }
    return pattern;
}

// The [from, to) range a verb names, or nothing (with the reason recorded) when it is missing or empty.
std::optional<std::pair<double, double>> parseRange(const Flags& flags) {
    auto fromRaw = requireFlag(flags, "from");
    auto toRaw = requireFlag(flags, "to");
    auto from = fromRaw ? safeStof(*fromRaw) : std::nullopt;
    auto to = toRaw ? safeStof(*toRaw) : std::nullopt;
    if (!from || !to)
        return std::nullopt;
    if (*from < 0.0f || *to <= *from) {
        CommandRegistry::fail("the range needs 0 <= from < to");
        return std::nullopt;
    }
    return std::make_pair(static_cast<double>(*from), static_cast<double>(*to));
}

// 0 = every unit; nothing when the flag is present but malformed.
std::optional<uint64_t> optionalUnit(const Flags& flags) {
    auto raw = requireFlag(flags, "unit");
    if (!raw)
        return uint64_t{0};
    return safeStoull(*raw);
}

// The length a pattern must have to hold notes ending at @p endBeat: unchanged when it already does, otherwise grown
// by whole bars, as the piano roll grows it for a duplicate (PianoRollDuplicate::barsToGrow). 0 means "leave it".
double grownLength(const PatternSource& pattern, double endBeat, const CommandContext& ctx) {
    const double bar = std::max(1.0, ctx.engine ? static_cast<double>(ctx.engine->getBeatsPerBar()) : 4.0);
    if (endBeat <= pattern.lengthBeats + 1e-9)
        return 0.0;
    const double bars = std::ceil((endBeat - pattern.lengthBeats) / bar - 1e-9);
    return pattern.lengthBeats + bars * bar;
}

const std::vector<MidiNote>& notesOf(const PatternSource& pattern) {
    return std::get<MidiPayload>(pattern.payload).notes;
}
} // namespace

void registerPianoRollVerbs(CommandRegistry& reg) {
    // add_notes: "start,pitch,duration[,velocity]; ..." for one unit. The whole batch is checked before anything is
    // written, so a bad entry leaves the pattern as it was.
    reg.registerCommand("add_notes", [](const Flags& flags, const CommandContext& ctx) -> std::unique_ptr<ICommand> {
        TrackManager* tm = ctx.trackManager;
        if (!tm)
            return nullptr;
        PatternID id;
        const PatternSource* pattern = midiPattern(*tm, flags, id);
        if (!pattern)
            return nullptr;
        auto unitRaw = requireFlag(flags, "unit");
        auto unit = unitRaw ? safeStoull(*unitRaw) : std::nullopt;
        if (!unit)
            return nullptr;
        if (!tm->getUnitManager().getUnit(*unit))
            return CommandRegistry::fail("no such unit: " + std::to_string(*unit));
        auto listRaw = requireFlag(flags, "notes");
        if (!listRaw)
            return nullptr;

        std::vector<MidiNote> added;
        double end = 0.0;
        for (const std::string& entry : split(std::string(*listRaw), ';')) {
            if (entry.empty())
                continue;
            const auto fields = split(entry, ',');
            const auto bad = [&](const std::string& why) {
                return CommandRegistry::fail("add_notes: \"" + entry + "\": " + why +
                                             " (each entry is start,pitch,duration[,velocity])");
            };
            if (fields.size() < 3 || fields.size() > 4)
                return bad("expected 3 or 4 fields");
            auto start = safeStof(fields[0]);
            auto pitch = safeStoi(fields[1]);
            auto duration = safeStof(fields[2]);
            std::optional<float> velocity = 0.8f;
            if (fields.size() == 4)
                velocity = safeStof(fields[3]);
            if (!start || *start < 0.0f)
                return bad("start must be a beat >= 0");
            if (!pitch || *pitch < 0 || *pitch > 127)
                return bad("pitch must be 0..127");
            if (!duration || *duration <= 0.0f)
                return bad("duration must be > 0");
            if (!velocity || *velocity < 0.0f || *velocity > 1.0f)
                return bad("velocity must be 0..1");
            MidiNote note;
            note.pitch = *pitch;
            note.startBeat = *start;
            note.durationBeats = *duration;
            note.velocity = *velocity;
            note.unitId = *unit;
            end = std::max(end, note.startBeat + note.durationBeats);
            added.push_back(note);
            if (added.size() > kMaxBatchNotes)
                return CommandRegistry::fail("add_notes takes at most " + std::to_string(kMaxBatchNotes) +
                                             " notes per call");
        }
        if (added.empty())
            return CommandRegistry::fail("add_notes: no notes given");

        std::vector<MidiNote> after = notesOf(*pattern);
        after.insert(after.end(), added.begin(), added.end());
        return std::make_unique<ReplacePatternNotesCommand>(tm->getPatternManager(), id, std::move(after),
                                                            grownLength(*pattern, end, ctx), "Add Notes");
    });

    reg.registerCommand("set_note_length",
                        [](const Flags& flags, const CommandContext& ctx) -> std::unique_ptr<ICommand> {
        TrackManager* tm = ctx.trackManager;
        if (!tm)
            return nullptr;
        PatternID id;
        const PatternSource* pattern = midiPattern(*tm, flags, id);
        if (!pattern)
            return nullptr;
        auto unitRaw = requireFlag(flags, "unit");
        auto pitchRaw = requireFlag(flags, "pitch");
        auto startRaw = requireFlag(flags, "start");
        auto durationRaw = requireFlag(flags, "duration");
        auto unit = unitRaw ? safeStoull(*unitRaw) : std::nullopt;
        auto pitch = pitchRaw ? safeStoi(*pitchRaw) : std::nullopt;
        auto start = startRaw ? safeStof(*startRaw) : std::nullopt;
        auto duration = durationRaw ? safeStof(*durationRaw) : std::nullopt;
        if (!unit || !pitch || !start || !duration)
            return nullptr;
        if (*duration <= 0.0f)
            return CommandRegistry::fail("duration must be > 0");
        // Same key and tolerance as the other note verbs: the start round-tripped through JSON text.
        for (const MidiNote& note : notesOf(*pattern)) {
            if (note.pitch == *pitch && note.unitId == *unit && std::abs(note.startBeat - *start) < 1e-6)
                return std::make_unique<ResizeNoteCommand>(tm->getPatternManager(), id, note,
                                                           static_cast<double>(*duration));
        }
        return CommandRegistry::fail("no note at pattern " + std::to_string(id.value) + ", unit " +
                                     std::to_string(*unit) + ", pitch " + std::to_string(*pitch) + ", start " +
                                     beats(*start));
    });

    // delete_notes: every note that starts in [from, to), optionally only one unit's, optionally only a pitch span.
    reg.registerCommand("delete_notes", [](const Flags& flags, const CommandContext& ctx) -> std::unique_ptr<ICommand> {
        TrackManager* tm = ctx.trackManager;
        if (!tm)
            return nullptr;
        PatternID id;
        const PatternSource* pattern = midiPattern(*tm, flags, id);
        if (!pattern)
            return nullptr;
        const auto range = parseRange(flags);
        const auto unit = optionalUnit(flags);
        if (!range || !unit)
            return nullptr;
        int low = 0, high = 127;
        if (auto raw = requireFlag(flags, "pitch_low")) {
            auto v = safeStoi(*raw);
            if (!v)
                return nullptr;
            low = *v;
        }
        if (auto raw = requireFlag(flags, "pitch_high")) {
            auto v = safeStoi(*raw);
            if (!v)
                return nullptr;
            high = *v;
        }
        if (low > high)
            return CommandRegistry::fail("pitch_low is above pitch_high");

        std::vector<MidiNote> after;
        for (const MidiNote& n : notesOf(*pattern)) {
            const bool hit = startsIn(n, range->first, range->second) && (*unit == 0 || n.unitId == *unit) &&
                             n.pitch >= low && n.pitch <= high;
            if (!hit)
                after.push_back(n);
        }
        if (after.size() == notesOf(*pattern).size())
            return CommandRegistry::fail("no notes start in beats " + beats(range->first) + " to " +
                                         beats(range->second) + " that match");
        return std::make_unique<ReplacePatternNotesCommand>(tm->getPatternManager(), id, std::move(after), 0.0,
                                                            "Delete Notes");
    });

    // duplicate_notes: FD-52's ruler duplicate. The notes that start in [from, to) are copied into the next block of
    // the same length, [to, to + (to - from)), and the pattern grows by whole bars when the copy runs past its end.
    // Calling it again with that next block keeps extending the phrase, as pressing Duplicate again does.
    reg.registerCommand("duplicate_notes",
                        [](const Flags& flags, const CommandContext& ctx) -> std::unique_ptr<ICommand> {
        TrackManager* tm = ctx.trackManager;
        if (!tm)
            return nullptr;
        PatternID id;
        const PatternSource* pattern = midiPattern(*tm, flags, id);
        if (!pattern)
            return nullptr;
        const auto range = parseRange(flags);
        const auto unit = optionalUnit(flags);
        if (!range || !unit)
            return nullptr;

        const double shift = range->second - range->first;
        std::vector<MidiNote> after = notesOf(*pattern);
        double end = range->second + shift; // the block the copies land in must fit, as the moved highlight does
        size_t copied = 0;
        for (const MidiNote& n : notesOf(*pattern)) {
            if (!startsIn(n, range->first, range->second) || (*unit != 0 && n.unitId != *unit))
                continue;
            MidiNote c = n;
            c.startBeat += shift;
            end = std::max(end, c.startBeat + c.durationBeats);
            after.push_back(c);
            ++copied;
        }
        if (copied == 0)
            return CommandRegistry::fail("no notes start in beats " + beats(range->first) + " to " +
                                         beats(range->second) + " to duplicate");
        return std::make_unique<ReplacePatternNotesCommand>(tm->getPatternManager(), id, std::move(after),
                                                            grownLength(*pattern, end, ctx), "Duplicate Notes");
    });
}

} // namespace Audio
} // namespace Aestra
