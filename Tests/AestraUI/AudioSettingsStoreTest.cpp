// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// One parser for audio_settings.conf (#649).
//
// Before this, the file had two readers that disagreed about its contents:
// AestraAudioController parsed `device`/`input_device` only, while
// AudioSettingsPage parsed all thirteen keys but could apply none of them. The
// startup path therefore opened the stream with hard-coded literals —
//
//     config.sampleRate = 48000;
//     config.bufferSize = 512;
//
// — while the user's file said 256, and the Settings dialog displayed 256 with
// an "Est. Latency: 5 ms" derived from it. Observed on a real machine.
//
// The property that matters most here is the UNSET/DEFAULT distinction. "Absent
// from the file" and "set to the default" must not be the same value, because
// the caller has to choose between leaving a driver-negotiated setting alone and
// overwriting it. Most of the assertions below exist to pin that.

#include "AudioSettingsStore.h"

#include <iostream>
#include <sstream>
#include <string>

using Aestra::AudioSettings;
using Aestra::isPlausibleBufferSize;
using Aestra::isPlausibleDitheringMode;
using Aestra::isPlausibleResamplingIndex;
using Aestra::isPlausibleSampleRate;
using Aestra::isSet;
using Aestra::parseAudioSettings;
using Aestra::serializeAudioSettings;

namespace {

int g_failures = 0;

void expectEq(int got, int wanted, const std::string& what) {
    if (got != wanted) {
        std::cerr << "[FAIL] " << what << ": got " << got << ", wanted " << wanted << '\n';
        ++g_failures;
    }
}

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

AudioSettings parseText(const std::string& text) {
    std::istringstream in(text);
    return parseAudioSettings(in);
}

std::string serializeText(const AudioSettings& s) {
    std::ostringstream out;
    serializeAudioSettings(out, s);
    return out.str();
}

// The exact file from the reporting machine.
const char* kRealConfig =
    "driver=0\n"
    "device=0\n"
    "input_device=0\n"
    "samplerate=48000\n"
    "buffersize=256\n"
    "quality_preset=2\n"
    "resampling=3\n"
    "dithering=1\n"
    "threads=3\n"
    "dc_removal=0\n"
    "master_limiter=0\n"
    "preview_ducking_db=6\n"
    "multi_threading=1\n";

} // namespace

int main() {
    // --- the real file parses completely -------------------------------------
    {
        const AudioSettings s = parseText(kRealConfig);
        expectEq(s.driver, 0, "driver");
        expectEq(s.deviceId, 0, "device");
        expectEq(s.inputDeviceId, 0, "input_device");
        expectEq(s.sampleRate, 48000, "samplerate");
        expectEq(s.bufferSize, 256, "buffersize — the value startup was ignoring");
        expectEq(s.qualityPreset, 2, "quality_preset");
        expectEq(s.resampling, 3, "resampling");
        expectEq(s.dithering, 1, "dithering");
        expectEq(s.threads, 3, "threads");
        expectEq(s.dcRemoval, 0, "dc_removal");
        expectEq(s.masterLimiter, 0, "master_limiter — the value the engine defaulted over");
        expectEq(s.previewDuckingDb, 6, "preview_ducking_db");
        expectEq(s.multiThreading, 1, "multi_threading");
    }

    // --- absent is NOT default ------------------------------------------------
    // The distinction the whole design rests on: a caller must be able to tell
    // "the user chose 512" from "there is no stored value, leave the negotiated
    // one alone".
    {
        const AudioSettings s = parseText("samplerate=44100\n");
        check(isSet(s.sampleRate), "present key is set");
        check(!isSet(s.bufferSize), "absent key is UNSET, not defaulted");
        check(!isSet(s.masterLimiter), "absent tri-state is UNSET, not false");
        check(!isSet(s.deviceId), "absent device is UNSET, not 0");

        const AudioSettings empty;
        check(!isSet(empty.sampleRate) && !isSet(empty.bufferSize) && !isSet(empty.masterLimiter),
              "default-constructed settings are entirely unset");
    }

    // 0 is a legal stored value and must be distinguishable from unset — the
    // same trap as #648, where an unpopulated control reporting 0 destroyed a
    // real device id.
    {
        const AudioSettings s = parseText("device=0\nmaster_limiter=0\ndc_removal=0\n");
        check(isSet(s.deviceId), "device=0 is SET");
        expectEq(s.deviceId, 0, "device=0 parses as 0");
        check(isSet(s.masterLimiter), "master_limiter=0 is SET");
        expectEq(s.masterLimiter, 0, "master_limiter=0 parses as 0 (off), not unset");
    }

    // --- malformed input leaves fields unset rather than defaulting -----------
    {
        const AudioSettings s = parseText("buffersize=notanumber\nsamplerate=48000\n");
        check(!isSet(s.bufferSize), "unparseable value leaves the field unset");
        expectEq(s.sampleRate, 48000, "a bad line does not discard the rest of the file");
    }
    {
        const AudioSettings s = parseText("buffersize=256abc\n");
        check(!isSet(s.bufferSize), "trailing garbage rejects the value entirely");
    }
    {
        const AudioSettings s = parseText("buffersize=99999999999999999999\n");
        check(!isSet(s.bufferSize), "out-of-range value is rejected, not truncated");
    }

    // --- tolerant of real-world file shapes ----------------------------------
    {
        const AudioSettings s = parseText("\n\n  buffersize = 128  \n# a comment\nnonsense\n=5\nkey=\nsamplerate=44100\n");
        expectEq(s.bufferSize, 128, "surrounding whitespace tolerated");
        expectEq(s.sampleRate, 44100, "comments, blank and malformed lines skipped");
    }
    {
        const AudioSettings s = parseText("unknown_key=7\nbuffersize=64\n");
        expectEq(s.bufferSize, 64, "unknown keys ignored without disturbing known ones");
    }
    {
        const AudioSettings s = parseText("buffersize=128\nbuffersize=512\n");
        expectEq(s.bufferSize, 512, "last occurrence wins");
    }

    // --- round trip -----------------------------------------------------------
    {
        const AudioSettings first = parseText(kRealConfig);
        const std::string out = serializeText(first);
        const AudioSettings second = parseText(out);

        expectEq(second.bufferSize, first.bufferSize, "round trip: buffersize");
        expectEq(second.masterLimiter, first.masterLimiter, "round trip: master_limiter");
        expectEq(second.deviceId, first.deviceId, "round trip: device");
        expectEq(second.threads, first.threads, "round trip: threads");
        check(serializeText(second) == out, "serialize is stable across repeated cycles");
    }

    // Unset fields are omitted, so a round trip never invents a value.
    {
        AudioSettings partial;
        partial.bufferSize = 256;
        const std::string out = serializeText(partial);
        check(out == "buffersize=256\n", "only set fields are written");

        const AudioSettings back = parseText(out);
        check(!isSet(back.sampleRate), "omitted field stays unset after a round trip");
        expectEq(back.bufferSize, 256, "set field survives");
    }

    // --- every key is both readable and writable ------------------------------
    // The asymmetry this prevents is the original defect in miniature: a key one
    // component could write and another did not know existed.
    {
        AudioSettings all;
        all.driver = 1; all.deviceId = 2; all.inputDeviceId = 3;
        all.sampleRate = 44100; all.bufferSize = 128; all.qualityPreset = 1;
        all.resampling = 2; all.dithering = 1; all.threads = 4;
        all.previewDuckingDb = 12; all.dcRemoval = 1; all.masterLimiter = 1;
        all.multiThreading = 0;

        const AudioSettings back = parseText(serializeText(all));
        expectEq(back.driver, 1, "rw driver");
        expectEq(back.deviceId, 2, "rw device");
        expectEq(back.inputDeviceId, 3, "rw input_device");
        expectEq(back.sampleRate, 44100, "rw samplerate");
        expectEq(back.bufferSize, 128, "rw buffersize");
        expectEq(back.qualityPreset, 1, "rw quality_preset");
        expectEq(back.resampling, 2, "rw resampling");
        expectEq(back.dithering, 1, "rw dithering");
        expectEq(back.threads, 4, "rw threads");
        expectEq(back.previewDuckingDb, 12, "rw preview_ducking_db");
        expectEq(back.dcRemoval, 1, "rw dc_removal");
        expectEq(back.masterLimiter, 1, "rw master_limiter");
        expectEq(back.multiThreading, 0, "rw multi_threading");
    }

    // --- presence is not sanity (review finding, #660) --------------------------
    // kUnset rules out exactly one value (-1), so a hand-edited or corrupted
    // config can still carry 0 or a negative through isSet() and reach a
    // consumer. buffersize=0 is the dangerous one: a divisor and an allocation
    // size. These are parsed faithfully — the store reports what the file said —
    // and rejected at the point of application.
    {
        const AudioSettings s = parseText("buffersize=0\nsamplerate=0\n");
        check(isSet(s.bufferSize), "buffersize=0 parses as SET (the file really said 0)");
        expectEq(s.bufferSize, 0, "buffersize=0 is preserved by the parser");
        check(!isPlausibleBufferSize(s.bufferSize), "buffersize=0 is rejected as implausible");
        check(!isPlausibleSampleRate(s.sampleRate), "samplerate=0 is rejected as implausible");
    }
    {
        const AudioSettings s = parseText("buffersize=-8\nsamplerate=-1\n");
        check(!isPlausibleBufferSize(s.bufferSize), "negative buffersize rejected");
        // samplerate=-1 collides with kUnset; it reads as absent, which is also safe.
        check(!isSet(s.sampleRate) || !isPlausibleSampleRate(s.sampleRate),
              "samplerate=-1 is either absent or implausible — never applied");
    }

    check(isPlausibleBufferSize(64) && isPlausibleBufferSize(256) && isPlausibleBufferSize(2048),
          "the buffer sizes the UI offers are all plausible");
    check(isPlausibleSampleRate(44100) && isPlausibleSampleRate(48000) && isPlausibleSampleRate(96000),
          "the sample rates the UI offers are all plausible");
    check(!isPlausibleBufferSize(15) && !isPlausibleBufferSize(16385), "buffer bounds are exclusive outside");
    check(isPlausibleBufferSize(16) && isPlausibleBufferSize(16384), "buffer bounds are inclusive inside");
    check(!isPlausibleSampleRate(7999) && !isPlausibleSampleRate(768001), "rate bounds exclusive outside");

    // DitheringMode has four enumerators; casting anything else to the enum and
    // handing it to the engine is not something the engine must survive.
    for (int v : {0, 1, 2, 3}) check(isPlausibleDitheringMode(v), "valid dithering mode accepted");
    for (int v : {-2, 4, 99}) check(!isPlausibleDitheringMode(v), "invalid dithering mode rejected");
    for (int v : {0, 1, 2, 3}) check(isPlausibleResamplingIndex(v), "valid resampling index accepted");
    for (int v : {-2, 4, 99}) check(!isPlausibleResamplingIndex(v), "invalid resampling index rejected");

    if (g_failures != 0) {
        std::cerr << "[FAIL] AudioSettingsStoreTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] AudioSettingsStoreTest\n";
    return 0;
}
