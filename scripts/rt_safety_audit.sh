#!/usr/bin/env bash
# rt_safety_audit.sh — static sweep of audio-callback-reachable sources for
# patterns that are forbidden on the real-time path (AGENTS §10).
#
# This is a TRIPWIRE, not a verdict: these files mix RT and non-RT functions,
# so every hit needs human classification. The classified baseline lives in
# Aestra-Internals: aestra-docs/rt-safety-audit.md — run this after touching RT code and explain
# any NEW hits there. The empirical allocation gate is RTAllocationTrapTest.
#
# Usage: scripts/rt_safety_audit.sh [--counts]
set -u
cd "$(dirname "$0")/.."

RT_SOURCES=(
    "AestraAudio/src/Core/AudioEngine.cpp"
    "AestraAudio/src/AudioRenderer.cpp"
    "AestraAudio/src/Core/MixerBus.cpp"
    "AestraAudio/src/Core/MixerChannel.cpp"
    "AestraAudio/src/Plugin/EffectChain.cpp"
    "AestraAudio/src/Playback/PatternPlaybackEngine.cpp"
    "AestraAudio/src/Playback/PreviewEngine.cpp"
    "AestraAudio/src/Playback/MetronomeEngine.cpp"
    "AestraAudio/src/Playback/AuditionEngine.cpp"
    "AestraAudio/src/Playback/TimelineClock.cpp"
    "Source/Core/AestraAudioController.cpp"
    "AestraAudio/src/Linux/RtAudioDriver.cpp"
)

# pattern|label
PATTERNS=(
    'std::mutex|mutex declaration'
    'lock_guard|std::scoped_lock|unique_lock|lock acquisition'
    'Aestra::Log::|Log::info|Log::warning|Log::error|logging call'
    '\bnew \[|\bnew [A-Za-z_]|operator new|make_shared|make_unique|heap allocation'
    '\.resize\(|\.reserve\(|push_back|emplace_back|container growth'
    'this_thread::sleep|usleep\(|nanosleep|sleep'
    'std::ifstream|std::ofstream|fopen\(|std::filesystem|file I/O'
)

counts_only=false
[ "${1:-}" = "--counts" ] && counts_only=true

total=0
for entry in "${PATTERNS[@]}"; do
    pattern="${entry%|*}"
    label="${entry##*|}"
    echo "== ${label} (${pattern}) =="
    n=0
    for f in "${RT_SOURCES[@]}"; do
        [ -f "$f" ] || continue
        if $counts_only; then
            c=$(grep -cE "$pattern" "$f" 2>/dev/null || true)
            [ "${c:-0}" -gt 0 ] && echo "  $f: $c" && n=$((n + c))
        else
            hits=$(grep -nE "$pattern" "$f" 2>/dev/null || true)
            if [ -n "$hits" ]; then
                echo "$hits" | sed "s|^|  $f:|"
                n=$((n + $(echo "$hits" | wc -l)))
            fi
        fi
    done
    echo "  -> ${n} hit(s)"
    echo ""
    total=$((total + n))
done

echo "TOTAL: ${total} hits across ${#RT_SOURCES[@]} RT-reachable sources."
echo "Compare against the classified baseline in Aestra-Internals: aestra-docs/rt-safety-audit.md."
