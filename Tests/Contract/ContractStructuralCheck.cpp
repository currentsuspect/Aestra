// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Ownership contract — structural checks (contract:ownership).
//
// Some contract rules live in app-shell code that no headless harness can drive
// (AestraContent, ArsenalPanel, UnitRow, SampleEditorPanel). Until the passes
// give that behavior a testable seam, these checks guard the rule by scanning
// the source tree for the patterns the contract removes (contract §8/§10).
// They are proxies, not behavioral proof: each pass that introduces a real
// seam should add a behavioral test next to the structural one.
//
// Modes (argv[1]):
//   sourceid-reads      guards F9       §3.2 no runtime read of ClipInstance::sourceId (serializer fallback exempt)
//   selection-playback  guards F16      §3.1 editor/selection code never rewinds the pattern scheduler
//   context-resolution  guards F17      §3.1 no ad-hoc unit resolution or find-pattern-by-name outside EditorContext
//   fake-waveform       guards F14      §8   no fabricated waveform in the sample editor
//   unit-lifecycle      guards F15,F22  §3.5 UI code never mutates unit identity/lifecycle directly
//   sampler-edits       guards F21      §3.5 UI code never calls sampler setters/destructive edits directly
//
// Setup problems (unreadable tree, a scanned file missing) abort, so WILL_FAIL
// can never mask them.

#include "../Contract/ContractSupport.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#ifndef AESTRA_SOURCE_ROOT
#error "AESTRA_SOURCE_ROOT must be defined by the build"
#endif

namespace fs = std::filesystem;
using AestraContract::contractSetup;
using AestraContract::str;
using AestraContract::Verdict;

namespace {

const fs::path kRoot{AESTRA_SOURCE_ROOT};

struct Rule {
    std::vector<std::string> roots;  // directories or files, relative to the source root
    std::vector<std::string> exempt; // path substrings that are exempt
    std::regex pattern;
    // When set, only lines inside these function bodies are scanned. A body
    // starts at a line matching "<Name>(" at column 0 and ends at the next "}"
    // at column 0. Every listed function must be found (setup failure otherwise).
    std::vector<std::string> onlyInFunctions = {};
};

bool isSourceFile(const fs::path& p) {
    const auto ext = p.extension().string();
    return ext == ".cpp" || ext == ".h" || ext == ".hpp";
}

std::vector<fs::path> filesUnder(const std::string& rel) {
    const fs::path p = kRoot / rel;
    contractSetup(fs::exists(p), "scanned path does not exist: " + p.string());
    std::vector<fs::path> out;
    if (fs::is_regular_file(p)) {
        out.push_back(p);
        return out;
    }
    for (const auto& e : fs::recursive_directory_iterator(p)) {
        if (e.is_regular_file() && isSourceFile(e.path()))
            out.push_back(e.path());
    }
    contractSetup(!out.empty(), "no source files under " + p.string());
    return out;
}

// Returns every matching "file:line: text" hit.
std::vector<std::string> scan(const Rule& rule) {
    std::vector<std::string> hits;
    std::set<std::string> foundFunctions;
    for (const auto& root : rule.roots) {
        for (const auto& file : filesUnder(root)) {
            const std::string rel = fs::relative(file, kRoot).generic_string();
            bool skip = false;
            for (const auto& ex : rule.exempt)
                skip = skip || rel.find(ex) != std::string::npos;
            if (skip)
                continue;
            std::ifstream in(file);
            contractSetup(in.good(), "cannot read " + rel);
            std::string line;
            size_t n = 0;
            bool inScope = rule.onlyInFunctions.empty();
            while (std::getline(in, line)) {
                ++n;
                if (!rule.onlyInFunctions.empty()) {
                    for (const auto& fn : rule.onlyInFunctions) {
                        if (line.find(fn + "(") != std::string::npos && !line.empty() && line[0] != ' ' &&
                            line[0] != '\t') {
                            inScope = true;
                            foundFunctions.insert(fn);
                        }
                    }
                    if (inScope && line.rfind("}", 0) == 0) {
                        inScope = false;
                        continue;
                    }
                    if (!inScope)
                        continue;
                }
                if (std::regex_search(line, rule.pattern)) {
                    auto trimmed = line.substr(
                        line.find_first_not_of(" \t") == std::string::npos ? 0 : line.find_first_not_of(" \t"));
                    hits.push_back(rel + ":" + str(n) + ": " + trimmed.substr(0, 140));
                }
            }
        }
    }
    for (const auto& fn : rule.onlyInFunctions) {
        contractSetup(foundFunctions.count(fn) == 1, "scoped function not found: " + fn);
    }
    return hits;
}

int run(const std::string& findings, const std::string& what, const Rule& rule) {
    Verdict v(findings);
    const auto hits = scan(rule);
    if (hits.empty()) {
        v.check(true, what + ": no occurrences");
    }
    for (const auto& h : hits)
        v.check(false, what + ": " + h);
    return v.finish();
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = AestraContract::contractMode(argc, argv);
    const auto ecma = std::regex::ECMAScript;

    if (mode == "sourceid-reads") {
        // A read is `clip.sourceId` / `clip->sourceId` not followed by a plain `=`.
        return run("F9", "runtime read of ClipInstance::sourceId",
                   {{"Source", "AestraAudio/src", "AestraAudio/include", "AestraUI"},
                    {"Source/Core/ProjectSerializer.cpp"},
                    std::regex(R"((^|[^A-Za-z0-9_])[cC]lip(\.|->)sourceId\s*(==|!=|[^=\s]|$))", ecma)});
    }
    if (mode == "selection-playback") {
        // Context changes only: choosing the active pattern or unit. Pattern
        // edits (e.g. the Arsenal length commands) are model changes, not context.
        return run("F16", "editor-context change rewinds the scheduler",
                   {{"Source/Panels/ArsenalPanel.cpp"},
                    {},
                    std::regex(R"(preparePatternForArsenal\s*\(|rewindScheduledInstances\s*\()", ecma),
                    {"ArsenalPanel::setActivePattern", "ArsenalPanel::ensureDefaultPattern",
                     "ArsenalPanel::setSelectedUnit"}});
    }
    if (mode == "context-resolution") {
        // Ad-hoc "which unit owns this pattern" (first note's unit) and find-by-name.
        return run("F17", "unit/pattern resolution outside EditorContext",
                   {{"Source"}, {}, std::regex(R"(note\.unitId\s*!=\s*0\s*;\s*\}\)|name\s*==\s*"Pattern 1")", ecma)});
    }
    if (mode == "fake-waveform") {
        return run("F14", "fabricated waveform data",
                   {{"Source/Panels/SampleEditorPanel.cpp"}, {}, std::regex(R"(std::sin\(\s*t\s*\*\s*20\.0f)", ecma)});
    }
    if (mode == "unit-lifecycle") {
        return run(
            "F15,F22", "direct unit lifecycle/identity mutation from UI",
            {{"Source/Core", "Source/Panels", "Source/Components", "AestraUI"},
             {},
             std::regex(R"((getUnitManager\(\)|unitMgr|unitManager|m_manager)\s*\.\s*)"
                        R"((createUnit|removeUnit|duplicateUnit|attachPlugin|setUnitName|setUnitColor|setUnitType|)"
                        R"(setUnitAudioClip|setUnitAudioClipFromDecoded)\s*\()",
                        ecma)});
    }
    if (mode == "sampler-edits") {
        return run("F21", "direct sampler edit from UI",
                   {{"Source", "AestraUI"},
                    {},
                    std::regex(R"(sampler->(set[A-Z]\w*|normalizeSample|reverseSample)\s*\()", ecma)});
    }
    contractSetup(false, "unknown mode " + mode);
    return 2;
}
