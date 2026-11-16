// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file MusicalRole.h
 * @brief What a sound, track or channel IS in the song: the shared piece of project understanding.
 *
 * Connected Understanding (owner brief, 16 Nov 2026): the Arsenal, Timeline, Mixer and Audition should share what
 * the user is making, so that something said in one place informs the others. A role is that shared fact, and the
 * rules for it are the brief's:
 *
 *   1. An explicit choice by the user wins, always (RoleSource::User).
 *   2. A recognised naming convention ("Kick 01", "Vocal Lead") gives a role the user can still override
 *      (RoleSource::Name).
 *   3. Anything else is Unknown. Aestra does not pretend to understand what it has not been given enough to
 *      understand: "Audio 17" is not drums because it sounds like a kick.
 *
 * The name rules are deterministic and cheap: no inference, no listening. An ambiguous name ("bass kick") is
 * Unknown, never a guess. Header-only and free of engine types, so it is testable everywhere and the Arsenal,
 * Timeline, Mixer and Audition all link the one rule set.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Aestra {
namespace Audio {

enum class RoleCategory : uint8_t {
    Unknown = 0,
    Drums,
    Percussion,
    Bass,
    Vocals,
    Keys,
    Guitar,
    Synth,
    Strings,
    Fx,
};

/// The part within a category. Only meaningful beside a category; None says "the category, nothing finer".
enum class RoleSub : uint8_t {
    None = 0,
    Kick,
    Snare,
    Clap,
    HiHat,
    Tom,
    Cymbal,
    Shaker,
    Tambourine,
    Lead,
    Backing,
    Double,
};

/// Where a role came from. User outranks Name; None means no role is known.
enum class RoleSource : uint8_t { None = 0, Name = 1, User = 2 };

struct MusicalRole {
    RoleCategory category{RoleCategory::Unknown};
    RoleSub sub{RoleSub::None};
    RoleSource source{RoleSource::None};

    bool known() const noexcept { return category != RoleCategory::Unknown; }
    bool operator==(const MusicalRole& o) const noexcept {
        return category == o.category && sub == o.sub && source == o.source;
    }
    bool operator!=(const MusicalRole& o) const noexcept { return !(*this == o); }
};

// -----------------------------------------------------------------------------------------------------------------
// Names: stable ids for saving, and words for showing
// -----------------------------------------------------------------------------------------------------------------

struct RoleNameEntry {
    RoleCategory category;
    const char* id;    ///< what a saved project says: never change one
    const char* label; ///< what the user reads
};

inline const std::vector<RoleNameEntry>& roleCategoryTable() {
    static const std::vector<RoleNameEntry> table = {
        {RoleCategory::Drums, "drums", "Drums"},       {RoleCategory::Percussion, "percussion", "Percussion"},
        {RoleCategory::Bass, "bass", "Bass"},          {RoleCategory::Vocals, "vocals", "Vocals"},
        {RoleCategory::Keys, "keys", "Keys"},          {RoleCategory::Guitar, "guitar", "Guitar"},
        {RoleCategory::Synth, "synth", "Synth"},       {RoleCategory::Strings, "strings", "Strings"},
        {RoleCategory::Fx, "fx", "FX"},
    };
    return table;
}

struct RoleSubEntry {
    RoleSub sub;
    const char* id;
    const char* label;
};

inline const std::vector<RoleSubEntry>& roleSubTable() {
    static const std::vector<RoleSubEntry> table = {
        {RoleSub::Kick, "kick", "Kick"},         {RoleSub::Snare, "snare", "Snare"},
        {RoleSub::Clap, "clap", "Clap"},         {RoleSub::HiHat, "hihat", "Hi-hat"},
        {RoleSub::Tom, "tom", "Tom"},            {RoleSub::Cymbal, "cymbal", "Cymbal"},
        {RoleSub::Shaker, "shaker", "Shaker"},   {RoleSub::Tambourine, "tambourine", "Tambourine"},
        {RoleSub::Lead, "lead", "Lead"},         {RoleSub::Backing, "backing", "Backing"},
        {RoleSub::Double, "double", "Double"},
    };
    return table;
}

inline const char* roleCategoryId(RoleCategory c) {
    for (const auto& e : roleCategoryTable()) if (e.category == c) return e.id;
    return "";
}
inline const char* roleCategoryLabel(RoleCategory c) {
    for (const auto& e : roleCategoryTable()) if (e.category == c) return e.label;
    return "";
}
inline RoleCategory roleCategoryFromId(std::string_view id) {
    for (const auto& e : roleCategoryTable()) if (id == e.id) return e.category;
    return RoleCategory::Unknown;
}
inline const char* roleSubId(RoleSub s) {
    for (const auto& e : roleSubTable()) if (e.sub == s) return e.id;
    return "";
}
inline const char* roleSubLabel(RoleSub s) {
    for (const auto& e : roleSubTable()) if (e.sub == s) return e.label;
    return "";
}
inline RoleSub roleSubFromId(std::string_view id) {
    for (const auto& e : roleSubTable()) if (id == e.id) return e.sub;
    return RoleSub::None;
}

inline const char* roleSourceId(RoleSource s) {
    if (s == RoleSource::User) return "user";
    if (s == RoleSource::Name) return "name";
    return "";
}
inline RoleSource roleSourceFromId(std::string_view id) {
    if (id == "user") return RoleSource::User;
    if (id == "name") return RoleSource::Name;
    return RoleSource::None;
}

/// "Drums", or "Drums · Kick": the short text for a chip or a menu.
inline std::string roleLabel(const MusicalRole& role) {
    if (!role.known()) return {};
    std::string out = roleCategoryLabel(role.category);
    if (role.sub != RoleSub::None) out += std::string(" \xc2\xb7 ") + roleSubLabel(role.sub);
    return out;
}

/// A sub that does not belong with its category (a "Kick" of Vocals) is dropped, never kept.
inline bool roleSubFits(RoleCategory category, RoleSub sub) {
    switch (sub) {
    case RoleSub::None: return true;
    case RoleSub::Kick: case RoleSub::Snare: case RoleSub::Clap: case RoleSub::HiHat: case RoleSub::Tom:
    case RoleSub::Cymbal: return category == RoleCategory::Drums;
    case RoleSub::Shaker: case RoleSub::Tambourine: return category == RoleCategory::Percussion;
    case RoleSub::Lead: case RoleSub::Backing: case RoleSub::Double: return category == RoleCategory::Vocals;
    }
    return false;
}

// -----------------------------------------------------------------------------------------------------------------
// Reading a name
// -----------------------------------------------------------------------------------------------------------------

namespace role_detail {

/// Words of a name, lower case: split on anything that is not a letter, on camelCase ("HiHat" -> hi hat) and on
/// letter/digit changes ("kick01" -> kick). A bare number is dropped, except "808", which is a word in itself.
inline std::vector<std::string> words(std::string_view name) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        const bool digits = std::all_of(cur.begin(), cur.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!digits || cur == "808") out.push_back(cur);
        cur.clear();
    };
    char prev = 0;
    for (unsigned char c : name) {
        const bool alpha = std::isalpha(c) != 0;
        const bool digit = std::isdigit(c) != 0;
        if (!alpha && !digit) { flush(); prev = 0; continue; }
        const bool camel = alpha && std::isupper(c) && std::islower(static_cast<unsigned char>(prev));
        const bool turn = (digit && std::isalpha(static_cast<unsigned char>(prev))) ||
                          (alpha && std::isdigit(static_cast<unsigned char>(prev)));
        if (camel || turn) flush();
        cur.push_back(static_cast<char>(std::tolower(c)));
        prev = static_cast<char>(c);
    }
    flush();
    return out;
}

struct Cue {
    RoleCategory category;
    RoleSub sub;
};

/// One word that names a role by itself. Deliberately short: a word that means two things ("pad", "key", "sub",
/// "bd", "lead" alone) is not here, because a wrong guess costs more than a missing one.
inline bool wordCue(const std::string& w, Cue& cue) {
    struct Row { const char* word; RoleCategory category; RoleSub sub; };
    static const Row rows[] = {
        {"drum", RoleCategory::Drums, RoleSub::None},        {"drums", RoleCategory::Drums, RoleSub::None},
        {"kit", RoleCategory::Drums, RoleSub::None},         {"kick", RoleCategory::Drums, RoleSub::Kick},
        {"snare", RoleCategory::Drums, RoleSub::Snare},      {"clap", RoleCategory::Drums, RoleSub::Clap},
        {"claps", RoleCategory::Drums, RoleSub::Clap},       {"hat", RoleCategory::Drums, RoleSub::HiHat},
        {"hats", RoleCategory::Drums, RoleSub::HiHat},       {"hihat", RoleCategory::Drums, RoleSub::HiHat},
        {"hihats", RoleCategory::Drums, RoleSub::HiHat},     {"hh", RoleCategory::Drums, RoleSub::HiHat},
        {"tom", RoleCategory::Drums, RoleSub::Tom},          {"toms", RoleCategory::Drums, RoleSub::Tom},
        {"cymbal", RoleCategory::Drums, RoleSub::Cymbal},    {"cymbals", RoleCategory::Drums, RoleSub::Cymbal},
        {"crash", RoleCategory::Drums, RoleSub::Cymbal},     {"ride", RoleCategory::Drums, RoleSub::Cymbal},
        {"perc", RoleCategory::Percussion, RoleSub::None},   {"percussion", RoleCategory::Percussion, RoleSub::None},
        {"shaker", RoleCategory::Percussion, RoleSub::Shaker},
        {"tambourine", RoleCategory::Percussion, RoleSub::Tambourine},
        {"tamb", RoleCategory::Percussion, RoleSub::Tambourine},
        {"conga", RoleCategory::Percussion, RoleSub::None},  {"bongo", RoleCategory::Percussion, RoleSub::None},
        {"cowbell", RoleCategory::Percussion, RoleSub::None},
        {"bass", RoleCategory::Bass, RoleSub::None},         {"808", RoleCategory::Bass, RoleSub::None},
        {"vocal", RoleCategory::Vocals, RoleSub::None},      {"vocals", RoleCategory::Vocals, RoleSub::None},
        {"vox", RoleCategory::Vocals, RoleSub::None},        {"voc", RoleCategory::Vocals, RoleSub::None},
        {"choir", RoleCategory::Vocals, RoleSub::None},
        {"piano", RoleCategory::Keys, RoleSub::None},        {"keys", RoleCategory::Keys, RoleSub::None},
        {"keyboard", RoleCategory::Keys, RoleSub::None},     {"rhodes", RoleCategory::Keys, RoleSub::None},
        {"organ", RoleCategory::Keys, RoleSub::None},        {"wurli", RoleCategory::Keys, RoleSub::None},
        {"guitar", RoleCategory::Guitar, RoleSub::None},     {"gtr", RoleCategory::Guitar, RoleSub::None},
        {"synth", RoleCategory::Synth, RoleSub::None},
        {"strings", RoleCategory::Strings, RoleSub::None},   {"violin", RoleCategory::Strings, RoleSub::None},
        {"viola", RoleCategory::Strings, RoleSub::None},     {"cello", RoleCategory::Strings, RoleSub::None},
        {"fx", RoleCategory::Fx, RoleSub::None},             {"riser", RoleCategory::Fx, RoleSub::None},
        {"impact", RoleCategory::Fx, RoleSub::None},         {"whoosh", RoleCategory::Fx, RoleSub::None},
        {"sweep", RoleCategory::Fx, RoleSub::None},
    };
    for (const auto& r : rows)
        if (w == r.word) { cue = {r.category, r.sub}; return true; }
    return false;
}

/// Two words that mean one thing, or that must not be read as their parts ("bass drum" is a kick).
inline bool compoundCue(const std::string& a, const std::string& b, Cue& cue) {
    if (a == "bass" && b == "drum") { cue = {RoleCategory::Drums, RoleSub::Kick}; return true; }
    if (a == "bass" && (b == "guitar" || b == "gtr")) { cue = {RoleCategory::Bass, RoleSub::None}; return true; }
    if ((a == "hi" || a == "open" || a == "closed") && b == "hat") { cue = {RoleCategory::Drums, RoleSub::HiHat}; return true; }
    if (a == "snare" && b == "drum") { cue = {RoleCategory::Drums, RoleSub::Snare}; return true; }
    if (a == "kick" && b == "drum") { cue = {RoleCategory::Drums, RoleSub::Kick}; return true; }
    return false;
}

/// A word that only refines a vocal ("lead", "backing", "double").
inline RoleSub vocalModifier(const std::string& w) {
    if (w == "lead") return RoleSub::Lead;
    if (w == "backing" || w == "bgv") return RoleSub::Backing;
    if (w == "double" || w == "dbl") return RoleSub::Double;
    return RoleSub::None;
}

/// The one category the cues agree on, with the sub they agree on (None when they differ); Unknown when the cues
/// name different categories or none at all.
inline Cue foldCues(const std::vector<Cue>& cues) {
    if (cues.empty()) return {RoleCategory::Unknown, RoleSub::None};
    const RoleCategory category = cues.front().category;
    RoleSub sub = RoleSub::None;
    bool subSet = false;
    bool subConflict = false;
    for (const auto& c : cues) {
        if (c.category != category) return {RoleCategory::Unknown, RoleSub::None};
        if (c.sub == RoleSub::None) continue;
        if (!subSet) { sub = c.sub; subSet = true; }
        else if (sub != c.sub) subConflict = true;
    }
    return {category, subConflict ? RoleSub::None : sub};
}

} // namespace role_detail

/**
 * @brief The role a name gives, or Unknown. Never a guess: a name that names two different things is Unknown.
 *
 * "Kick 01" -> Drums, Kick. "hh - huge * umbra" -> Drums, Hi-hat. "Vocal Lead" -> Vocals, Lead. "Piano Chords" ->
 * Keys. "bass drum" -> Drums, Kick (a compound, not Bass and Drums). "bass guitar" -> Bass. "Audio 17" -> Unknown.
 * "bass kick" -> Unknown (two things).
 */
inline MusicalRole roleFromName(std::string_view name) {
    using role_detail::Cue;
    const std::vector<std::string> w = role_detail::words(name);
    std::vector<Cue> cues;
    RoleSub modifier = RoleSub::None; // lead / backing / double: only means something beside a vocal

    for (size_t i = 0; i < w.size(); ++i) {
        Cue cue{};
        if (i + 1 < w.size() && role_detail::compoundCue(w.at(i), w.at(i + 1), cue)) {
            cues.push_back(cue);
            ++i;
            continue;
        }
        if (const RoleSub m = role_detail::vocalModifier(w.at(i)); m != RoleSub::None) { modifier = m; continue; }
        if (role_detail::wordCue(w.at(i), cue)) cues.push_back(cue);
    }

    Cue folded = role_detail::foldCues(cues);
    if (folded.category == RoleCategory::Unknown) return {}; // nothing named, or two different things: not a guess
    if (folded.sub == RoleSub::None && folded.category == RoleCategory::Vocals) folded.sub = modifier;
    if (!roleSubFits(folded.category, folded.sub)) folded.sub = RoleSub::None;
    return {folded.category, folded.sub, RoleSource::Name};
}

// -----------------------------------------------------------------------------------------------------------------
// The precedence rules
// -----------------------------------------------------------------------------------------------------------------

/// The user's choice for @p category (and @p sub): confirmed, and it outranks every name.
inline MusicalRole userRole(RoleCategory category, RoleSub sub = RoleSub::None) {
    if (category == RoleCategory::Unknown) return {};
    return {category, roleSubFits(category, sub) ? sub : RoleSub::None, RoleSource::User};
}

/// A thing was named (or renamed) @p name. The user's own choice stands; otherwise the role is whatever the name
/// gives now, which may be nothing: a role read from an old name does not outlive the name.
inline MusicalRole roleAfterNaming(const MusicalRole& current, std::string_view name) {
    if (current.source == RoleSource::User) return current;
    return roleFromName(name);
}

} // namespace Audio
} // namespace Aestra
