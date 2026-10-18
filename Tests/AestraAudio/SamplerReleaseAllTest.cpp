// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// requestReleaseAllVoices(): the soft release a pause, a stop and a timeline loop wrap use.
//
// The bug (owner, 2026-10-11): notes held when the timeline looped or paused kept ringing until
// their sample ended, however tight the ADSR release was, because the transport jump discards the
// queued note-offs. This pins the sampler half of the fix: every sounding voice enters its own
// release stage, a note that starts in the same block still sounds, and a voice that is already
// releasing keeps its tail instead of being restarted.

#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr uint8_t kNote = 60;
constexpr uint8_t kVelocity = 110;
// A held 0.3-peak sine at velocity 110 measures ~0.0495 at the sampler output (velocity and pan law),
// so 'audible' is anything above 0.02 and 'silent' anything below 1e-4.
constexpr float kAudible = 0.02f;

int g_failures = 0;
void expect(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

void le16(std::ofstream& o, uint16_t v) { o.put(char(v & 0xff)); o.put(char((v >> 8) & 0xff)); }
void le32(std::ofstream& o, uint32_t v) { le16(o, uint16_t(v & 0xffff)); le16(o, uint16_t(v >> 16)); }

// 2 s of 220 Hz sine at 0.3: far longer than anything the test renders, so any sound that
// is still there after the release time can only be a voice that never released.
bool writeWav(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const uint32_t frames = kSampleRate * 2, bytes = frames * 2;
    out.write("RIFF", 4); le32(out, 36 + bytes); out.write("WAVEfmt ", 8); le32(out, 16);
    le16(out, 1); le16(out, 1); le32(out, kSampleRate); le32(out, kSampleRate * 2); le16(out, 2); le16(out, 16);
    out.write("data", 4); le32(out, bytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = double(i) / kSampleRate;
        le16(out, uint16_t(int16_t(std::sin(6.28318530717958647692 * 220.0 * t) * 0.3 * 32767.0)));
    }
    return true;
}

struct Rig {
    Plugins::SamplerPlugin sampler;
    std::vector<float> l = std::vector<float>(kBlock), r = std::vector<float>(kBlock);
    bool init(const std::filesystem::path& wav, float release) {
        if (!sampler.initialize(kSampleRate, kBlock) || !sampler.loadSample(wav.string())) return false;
        sampler.setRootMidiNote(60);
        sampler.setMaxVoices(8);
        sampler.setEnvelope(0.001f, 0.05f, 1.0f, release); // full sustain: a held note never decays by itself
        sampler.activate();
        return true;
    }
    float block(const MidiBuffer* midi) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* outs[] = {l.data(), r.data()};
        sampler.process(nullptr, outs, 0, 2, kBlock, midi, nullptr);
        float peak = 0.0f;
        for (float v : l) peak = std::max(peak, std::abs(v));
        return peak;
    }
};

} // namespace

int main() {
    const auto wav = std::filesystem::temp_directory_path() /
                     ("release_all_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    if (!writeWav(wav)) { std::cerr << "[FAIL] cannot write wav\n"; return 1; }

    // 1. Control: a held note with no release request keeps sounding (this is the reported bug's baseline).
    {
        Rig rig;
        if (!rig.init(wav, 0.010f)) { std::cerr << "[FAIL] init\n"; return 1; }
        MidiBuffer on; on.addNoteOn(1, kNote, kVelocity, 0);
        rig.block(&on);
        float last = 0.0f;
        for (int b = 0; b < 12; ++b) last = rig.block(nullptr);
        expect(last > kAudible, "a held note without a release request is still sounding 12 blocks later");
    }

    // 2. The request releases through the ADSR: gone within release time + one block.
    {
        Rig rig;
        rig.init(wav, 0.010f); // 10 ms = 480 frames
        MidiBuffer on; on.addNoteOn(1, kNote, kVelocity, 0);
        rig.block(&on);
        for (int b = 0; b < 8; ++b) rig.block(nullptr);
        const float before = rig.block(nullptr);
        rig.sampler.requestReleaseAllVoices();
        rig.block(nullptr);               // the release block (a 10 ms tail fades inside it and the next)
        rig.block(nullptr);
        const float after = rig.block(nullptr);
        expect(before > kAudible, "the note is sounding before the request");
        expect(after < 1.0e-4f, "after the request the voice has released and is silent within the release time");
    }

    // 3. The release follows the configured time: a long release is still audible shortly after the request.
    {
        Rig rig;
        rig.init(wav, 0.300f);
        MidiBuffer on; on.addNoteOn(1, kNote, kVelocity, 0);
        rig.block(&on);
        for (int b = 0; b < 8; ++b) rig.block(nullptr);
        rig.sampler.requestReleaseAllVoices();
        rig.block(nullptr);
        const float soon = rig.block(nullptr);
        expect(soon > 0.01f, "a 300 ms release is honoured: the tail is still audible a block or two later (not a hard cut)");
        float tail = 0.0f;
        for (int b = 0; b < 80; ++b) tail = rig.block(nullptr); // ~430 ms later
        expect(tail < 1.0e-4f, "and it is silent once the release time has passed");
    }

    // 4. A note that starts in the same block as the request still sounds (loop wrap: the first notes of the loop).
    {
        Rig rig;
        rig.init(wav, 0.010f);
        MidiBuffer on; on.addNoteOn(1, kNote, kVelocity, 0);
        rig.block(&on);
        for (int b = 0; b < 8; ++b) rig.block(nullptr);
        rig.sampler.requestReleaseAllVoices();
        MidiBuffer fresh; fresh.addNoteOn(1, 64, kVelocity, 20);
        rig.block(&fresh);
        rig.block(nullptr);
        const float later = rig.block(nullptr);
        expect(later > kAudible, "a note-on in the same block as the request keeps sounding (the old voice released, the new one did not)");
    }

    // 5. A voice that is already releasing keeps its tail: the request does not restart or lengthen it.
    {
        Rig rig;
        rig.init(wav, 0.300f);
        MidiBuffer on; on.addNoteOn(1, kNote, kVelocity, 0);
        rig.block(&on);
        for (int b = 0; b < 8; ++b) rig.block(nullptr);
        MidiBuffer off; off.addNoteOff(1, kNote, 0, 0);
        rig.block(&off);
        for (int b = 0; b < 4; ++b) rig.block(nullptr);
        const float mid = rig.block(nullptr);
        rig.sampler.requestReleaseAllVoices();
        const float after = rig.block(nullptr);
        expect(after <= mid + 1.0e-6f, "a releasing voice is not restarted by the request (level does not jump back up)");
    }

    std::error_code ec;
    std::filesystem::remove(wav, ec);
    std::cout << (g_failures == 0 ? "[PASS] SamplerReleaseAllTest\n" : "[FAIL] SamplerReleaseAllTest\n");
    return g_failures == 0 ? 0 : 1;
}
