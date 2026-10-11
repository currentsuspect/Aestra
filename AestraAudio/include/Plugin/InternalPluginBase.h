// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include "RealtimeThreadGuard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

/// Declarative description of one automatable parameter.
///
/// Field order and defaults mirror PluginParameter on purpose: most built-ins
/// already declare a table literal of exactly this shape inline. Migrating one
/// to this struct is a rename of its table's type, not a rewrite of it.
struct ParamSpec {
    uint32_t id = 0;
    const char* name = "";
    const char* shortName = "";
    const char* unit = "";
    float defaultValue = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool isAutomatable = true;
    bool isBypass = false;
    bool isReadOnly = false;
    uint32_t stepCount = 0;
};

/// Ceiling on a built-in's parameter count. The base's storage reserves this
/// many slots and indexes them by ParamSpec::id, so every id must sit below it.
inline constexpr uint32_t kMaxInternalPluginParams = 48;

namespace ParamSpecCheck {

/// The table's length agrees with the count the plugin reports. The base walks
/// [paramSpecs(), paramSpecs() + paramSpecCount()), so a count larger than the
/// table reads past its end -- an enum value added without a table row.
template <size_t N>
constexpr bool sizeMatches(const ParamSpec (&)[N], uint32_t declaredCount) {
    return N == declaredCount && N <= kMaxInternalPluginParams;
}

/// Every id indexes the base's storage, so one at or past the ceiling writes
/// out of bounds on the first seedDefaults().
template <size_t N>
constexpr bool idsInRange(const ParamSpec (&specs)[N]) {
    for (size_t i = 0; i < N; ++i)
        if (specs[i].id >= kMaxInternalPluginParams)
            return false;
    return true;
}

/// The keyed state blob identifies a parameter by id alone, so two rows sharing
/// one would save twice and load into whichever findSpec() meets first.
template <size_t N>
constexpr bool idsUnique(const ParamSpec (&specs)[N]) {
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < i; ++j)
            if (specs[j].id == specs[i].id)
                return false;
    return true;
}

template <size_t N>
constexpr bool defaultsInRange(const ParamSpec (&specs)[N]) {
    for (size_t i = 0; i < N; ++i)
        if (!(specs[i].minValue <= specs[i].defaultValue && specs[i].defaultValue <= specs[i].maxValue))
            return false;
    return true;
}

/// isBypassed() reads the first bypass row it finds; a second would be ignored.
template <size_t N>
constexpr bool atMostOneBypass(const ParamSpec (&specs)[N]) {
    size_t rows = 0;
    for (size_t i = 0; i < N; ++i)
        if (specs[i].isBypass)
            ++rows;
    return rows <= 1;
}

} // namespace ParamSpecCheck

} // namespace Audio
} // namespace Aestra

/// Compile-time validation of a built-in's ParamSpec table against the count it
/// reports. Place it in the class body after paramSpecCount(). A table that
/// fails any of these does not build, rather than reading or writing out of
/// bounds the first time an instance is created.
#define AESTRA_VALIDATE_PARAM_SPECS(specs, count)                                                                     \
    static_assert(::Aestra::Audio::ParamSpecCheck::sizeMatches(specs, count),                                       \
                  "ParamSpec table length must equal the reported parameter count, and fit the base's storage");   \
    static_assert(::Aestra::Audio::ParamSpecCheck::idsInRange(specs),                                               \
                  "every ParamSpec id must be below kMaxInternalPluginParams");                                     \
    static_assert(::Aestra::Audio::ParamSpecCheck::idsUnique(specs), "ParamSpec ids must be unique");               \
    static_assert(::Aestra::Audio::ParamSpecCheck::defaultsInRange(specs),                                          \
                  "every ParamSpec default must lie within [minValue, maxValue]");                                  \
    static_assert(::Aestra::Audio::ParamSpecCheck::atMostOneBypass(specs), "at most one ParamSpec row may be bypass")

namespace Aestra {
namespace Audio {

/// Shared base for built-in effect plugins.
///
/// It owns the parts of IPluginInstance that were identical in all twelve:
/// parameter storage, the non-finite and range guards, getParameters(), the
/// binary state blob, and the stub methods for the editor window and the
/// watchdog. A plugin keeps its DSP, its table, and a few hooks.
///
/// It deliberately does not own audio. initialize(), activate() and process()
/// stay in the plugin: that is the part that is genuinely new work per effect
/// and the part a base class would obscure. This class is also not used by
/// third-party VST3/CLAP hosting — OutOfProcessPluginInstance keeps its own
/// path — so nothing here changes a plugin boundary.
///
/// State (AGENTS.md §12): the base writes a keyed v2 blob, {magic, version=2,
/// count, (id, value) * count}, and rejects v1 by decision -- FD-24 reset the
/// format while no released project existed. See saveState(). A plugin whose
/// blob carries extra fields (EQ's dynamic bands) overrides saveState()/
/// loadState() instead. Each plugin's table is checked at compile time with
/// AESTRA_VALIDATE_PARAM_SPECS.
class InternalPluginBase : public IPluginInstance {
public:
    ~InternalPluginBase() override = default;

    // ==================================================================
    // Subclass contract
    // ==================================================================

    /// The plugin's parameter table. Must outlive the instance.
    virtual const ParamSpec* paramSpecs() const AESTRA_RT_NONBLOCKING = 0;

    /// Number of rows in paramSpecs().
    virtual uint32_t paramSpecCount() const AESTRA_RT_NONBLOCKING = 0;

    /// Magic for the state blob. Stable across releases: it is what tells this
    /// plugin's saved state apart from another's.
    virtual uint32_t stateMagic() const = 0;

    /// Writes the table's defaults into storage.
    ///
    /// The subclass calls this, exactly once, on the first initialize() of a
    /// fresh instance — usually through seedDefaultsOnce(), which owns the
    /// gate. The base never calls it on the subclass's behalf: initialize()
    /// is not owned here, so a plugin that skipped the call would silently
    /// start with every parameter at 0.0f (Transient would come up fully dry
    /// and 12 dB down) rather than at its declared defaults.
    void seedDefaults() {
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            const ParamSpec& spec = *p;
            m_params[spec.id].store(spec.defaultValue, std::memory_order_relaxed);
        }
    }

    /// Called after a parameter is stored, so a plugin can invalidate derived
    /// state (filter coefficients, oversampling config) or mirror the value
    /// into a smoothed copy. Not called for a rejected value.
    ///
    /// Runs on the AUDIO THREAD: AudioEngine::renderTrack applies plugin-
    /// parameter automation through setParameter() every block. An override
    /// must stay non-blocking -- set a flag, store a value -- and must carry
    /// AESTRA_RT_NONBLOCKING itself so the RT effect check can see its body.
    virtual void onParameterChanged(uint32_t /*id*/, float /*value*/) AESTRA_RT_NONBLOCKING {}

    // ==================================================================
    // Parameters — owned here
    // ==================================================================

    std::vector<PluginParameter> getParameters() const override {
        std::vector<PluginParameter> params;
        params.reserve(paramSpecCount());
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            const ParamSpec& spec = *p;
            params.push_back(PluginParameter{spec.id,
                                             spec.name,
                                             spec.shortName,
                                             spec.unit,
                                             spec.defaultValue,
                                             spec.minValue,
                                             spec.maxValue,
                                             spec.isAutomatable,
                                             spec.isBypass,
                                             spec.isReadOnly,
                                             spec.stepCount});
        }
        return params;
    }

    uint32_t getParameterCount() const override { return paramSpecCount(); }

    float getParameter(uint32_t id) const AESTRA_RT_NONBLOCKING override {
        if (id >= kMaxSpecParams)
            return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    /// A non-finite value is rejected outright, leaving the previous value in
    /// place. std::clamp(NaN, 0, 1) is NaN, so a clamp alone does not stop
    /// one, and a NaN parameter poisons the DSP for the life of the instance.
    ///
    /// Audio-thread entry point: automation calls this from renderTrack.
    void setParameter(uint32_t id, float value) AESTRA_RT_NONBLOCKING override {
        if (id >= kMaxSpecParams)
            return;
        const ParamSpec* spec = findSpec(id);
        if (!spec || spec->isReadOnly)
            return;
        if (!std::isfinite(value))
            return;
        const float clamped = std::min(std::max(value, spec->minValue), spec->maxValue);
        m_params[id].store(clamped, std::memory_order_relaxed);
        onParameterChanged(id, clamped);
    }

    /// Render-thread automation contract (V8-A5): parameter storage here is
    /// atomic, so a store from the render thread is lock-free. The plugin owns
    /// smoothing: onParameterChanged() / process() ramp toward the target.
    bool supportsRealtimeAutomation() const noexcept override { return true; }
    void applyAutomation(uint32_t id, float normalizedValue) noexcept AESTRA_RT_NONBLOCKING override {
        setParameter(id, normalizedValue);
    }

    /// Display formatting. The generic form is unit + precision scaled to the
    /// declared range. A plugin overrides this where a parameter needs units a
    /// generic rule cannot know (semitones, mode names).
    std::string getParameterDisplay(uint32_t id) const override {
        const ParamSpec* spec = findSpec(id);
        if (!spec)
            return {};
        const float value = getParameter(id);
        const float span = spec->maxValue - spec->minValue;
        const char* pattern = span <= 1.0f ? "%.2f %s" : (span <= 10.0f ? "%.1f %s" : "%.0f %s");
        char buf[48];
        std::snprintf(buf, sizeof(buf), pattern, static_cast<double>(value), spec->unit);
        return buf;
    }

    // ==================================================================
    // State — owned here
    // ==================================================================

    /// The blob is keyed: {magic, version=2, count, (paramId, value) * count}.
    ///
    /// v1 was {magic, version=1, params[count]} — positional, with the array
    /// index doubling as the parameter id. That is what P3 removes, and the
    /// reason is not a size complaint. Under v1, three ordinary changes to a
    /// parameter table behave completely differently:
    ///
    ///   add a parameter    the old blob is short, so it was rejected outright
    ///                      and the plugin came back on defaults. Loud.
    ///   remove one         the old blob is long; trailing bytes ignored. Silent
    ///                      but harmless, provided the survivors held position.
    ///   REORDER the table  old slot 0 is applied to whichever parameter now owns
    ///                      id 0. SILENT AND WRONG, and every value is finite and
    ///                      in range, so nothing rejects it and nothing warns.
    ///
    /// The third is the one that matters, and it is the one nobody had measured.
    /// Keying by id makes parameter identity independent of table position, so
    /// all three become safe: an id present in the blob but absent from the table
    /// is ignored, an id in the table but absent from the blob keeps its default.
    ///
    /// v1 blobs are REJECTED, not read. They are a known historical format, and
    /// the version check runs before the count is ever read — without that, a v1
    /// blob's first parameter float would be parsed as the entry count and the
    /// result would be a plausible-looking load of the wrong values. FD-24
    /// records why there is nothing to preserve: no released project exists, and
    /// every one of the legacy readers this codebase carried (EQ's 8 versions
    /// and 7 readers, Comp's 7 filler indices, Verb's v1–v5) was archaeology by
    /// the end of it. The cost is a version boundary; the benefit is that no
    /// future developer inherits a compatibility surface they must keep testing.
    std::vector<uint8_t> saveState() const override {
        const uint32_t count = paramSpecCount();
        std::vector<uint8_t> blob(keyedBlobSize(count));
        writeHeader(blob.data(), stateMagic(), kStateVersion);
        const uint32_t entries = count;
        std::memcpy(blob.data() + sizeof(uint32_t) * 2, &entries, sizeof(entries));
        uint8_t* cursor = blob.data() + kKeyedHeaderSize;
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            const ParamSpec& spec = *p;
            const uint32_t id = spec.id;
            std::memcpy(cursor, &id, sizeof(id));
            cursor += sizeof(id);
            const float value = m_params[spec.id].load(std::memory_order_relaxed);
            std::memcpy(cursor, &value, sizeof(value));
            cursor += sizeof(value);
        }
        return blob;
    }
    /// Reads a keyed v2 blob, applying by id rather than by position.
    ///
    /// Two kinds of bad VALUE are handled differently, on purpose (#1015):
    ///
    ///  - **Non-finite rejects the blob.** Corruption, unambiguously: no build
    ///    widens a parameter's range to include NaN, so there is no innocent
    ///    version of it. This is the case AGENTS.md §12's "a corrupt blob must
    ///    not leave the instance half-updated" is about.
    ///
    ///  - **Out-of-range clamps.** Either corruption, or a legitimate value
    ///    written by a build whose range for that parameter was wider. Nothing
    ///    forces a range change to bump kStateVersion, so that is reachable.
    ///
    /// This is not a new policy: setParameter() above has always done exactly
    /// this, reject non-finite and clamp the rest. loadState was the odd one
    /// out, rejecting a whole blob where an interactive slider drag would have
    /// clamped and carried on.
    ///
    /// An id the blob carries but the table does not is IGNORED, which is what
    /// makes removing a parameter safe. An id the table carries but the blob does
    /// not KEEPS ITS DEFAULT, which is what makes adding one safe. A blob id at or
    /// beyond kMaxSpecParams is ignored rather than trusted, so a corrupt or
    /// hostile id cannot index past m_params.
    ///
    /// std::clamp here rather than setParameter's std::min/std::max pair, and
    /// that is not an inconsistency: the non-finite pass has already returned on
    /// NaN, and std::clamp(NaN, lo, hi) is NaN — which is why setParameter
    /// cannot use it.
    ///
    /// The classification pass completes before anything is written, so the
    /// "not half-updated" guarantee is the two-pass STRUCTURE and does not
    /// depend on how many parameters were bad.
    bool loadState(const std::vector<uint8_t>& state) override {
        // Header first, and the version check BEFORE the count is read. A v1 blob
        // is 8 + 4N bytes; read as v2, its first parameter's float bits would be
        // parsed as the entry count and the load would look successful while
        // restoring entirely the wrong values. That is the failure this format
        // exists to prevent, so it has to be impossible rather than unlikely.
        if (state.size() < kKeyedHeaderSize)
            return false;

        if (!headerMatches(state.data(), stateMagic(), kStateVersion))
            return false;

        uint32_t count = 0;
        std::memcpy(&count, state.data() + sizeof(uint32_t) * 2, sizeof(count));
        if (count > kMaxSpecParams)
            return false;
        // Zero entries for a plugin that HAS parameters is not a shape any writer
        // produces. It is exactly what a v1 blob whose first parameter is 0.0f
        // looks like when misread: 0.0f's bits are 0x00000000, so the "count"
        // parses as 0, which is inside kMaxSpecParams, and any blob of 12+ bytes
        // satisfies keyedBlobSize(0). Every other guard passes. Refusing it here
        // means the invariant does not depend SOLELY on the version check
        // happening to run first.
        if (count == 0 && paramSpecCount() > 0)
            return false;
        if (state.size() < keyedBlobSize(count))
            return false;

        // Pass 1: classify. Nothing here touches a parameter, so a rejection
        // anywhere in this loop leaves the instance exactly as it was.
        std::vector<std::pair<uint32_t, float>> entries;
        entries.reserve(count);
        const uint8_t* cursor = state.data() + kKeyedHeaderSize;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t id = 0;
            float value = 0.0f;
            std::memcpy(&id, cursor, sizeof(id));
            cursor += sizeof(id);
            std::memcpy(&value, cursor, sizeof(value));
            cursor += sizeof(value);
            if (id >= kMaxSpecParams)
                continue; // unknown to this table: a removed parameter, or junk
            const ParamSpec* spec = findSpec(id);
            if (!spec || spec->isReadOnly)
                continue;
            if (!std::isfinite(value))
                return false;
            entries.emplace_back(id, value);
        }

        // Pass 2: apply.
        for (const auto& entry : entries) {
            const ParamSpec* spec = findSpec(entry.first);
            if (!spec)
                continue;
            const float value = std::clamp(entry.second, spec->minValue, spec->maxValue);
            m_params[entry.first].store(value, std::memory_order_relaxed);
            onParameterChanged(entry.first, value);
        }
        return true;
    }

    // ==================================================================
    // Stubs — owned here
    // ==================================================================

    bool hasEditor() const override { return false; }
    bool openEditor(void* /*parentWindow*/) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
    bool resizeEditor(int /*width*/, int /*height*/) override { return false; }

    uint32_t getTailSamples() const override { return 0; }

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

protected:
    /// Ceiling on a plugin's parameter count. The built-in tables run 5
    /// (Transient) to 33 (EQ); this is what the shared blob reserves.
    static constexpr uint32_t kMaxSpecParams = kMaxInternalPluginParams;
    /// v1 was positional and is no longer read. v2 is keyed: each entry carries
    /// its own id, so parameter identity is independent of table order.
    static constexpr uint32_t kStateVersion = 2;

    /// v1's version number, named only so the reader's rejection is testable
    /// against a blob built to the real v1 layout.
    static constexpr uint32_t kStateVersionV1 = 1;

    /// Seeds defaults on the first initialize() of a fresh instance only.
    ///
    /// EffectChain::prepare() re-calls initialize() on the live instance during
    /// sample-rate and device changes, and that must preserve the user's
    /// parameters and any loaded project state — so the gate lives here rather
    /// than in each plugin's initialize(), where ten more migrations would each
    /// re-derive it.
    void seedDefaultsOnce() {
        if (!m_defaultsSeeded.exchange(true))
            seedDefaults();
    }

    /// Relaxed read for a plugin's own DSP. Storage lives in the base so the
    /// guard, the clamp and the blob stay in one place.
    float paramValue(uint32_t id) const AESTRA_RT_NONBLOCKING { return m_params[id].load(std::memory_order_relaxed); }

    /// The bypass knob, read the way every plugin reads it.
    bool isBypassed() const AESTRA_RT_NONBLOCKING {
        const ParamSpec* spec = findBypassSpec();
        return spec ? m_params[spec->id].load(std::memory_order_relaxed) > 0.5f : false;
    }

private:
    const ParamSpec* specsBegin() const AESTRA_RT_NONBLOCKING { return paramSpecs(); }
    const ParamSpec* specsEnd() const AESTRA_RT_NONBLOCKING { return paramSpecs() + paramSpecCount(); }

    /// v2 header: magic, version, count. The count is written explicitly rather
    /// than implied by the blob's length, because a keyed blob's length depends
    /// on how many entries the WRITER's table had — which is not necessarily the
    /// reader's.
    static constexpr size_t kKeyedHeaderSize = sizeof(uint32_t) * 3;
    static constexpr size_t kKeyedEntrySize = sizeof(uint32_t) + sizeof(float);

    static constexpr size_t keyedBlobSize(uint32_t count) { return kKeyedHeaderSize + kKeyedEntrySize * count; }

    /// v1 was {magic, version, params[count]} — positional, 8 + 4N bytes. Kept
    /// as a named constant only so the reader's rejection of it is explicit and
    /// testable; nothing writes it. See saveState() for why there is no reader.
    static constexpr size_t blobSizeV1(uint32_t count) { return sizeof(uint32_t) * 2 + sizeof(float) * count; }

    static void writeHeader(uint8_t* dst, uint32_t magic, uint32_t version) {
        std::memcpy(dst, &magic, sizeof(magic));
        std::memcpy(dst + sizeof(uint32_t), &version, sizeof(version));
    }

    static bool headerMatches(const uint8_t* src, uint32_t magic, uint32_t version) {
        uint32_t blobMagic = 0;
        uint32_t blobVersion = 0;
        std::memcpy(&blobMagic, src, sizeof(blobMagic));
        std::memcpy(&blobVersion, src + sizeof(uint32_t), sizeof(blobVersion));
        return blobMagic == magic && blobVersion == version;
    }

    /// v1 only. Kept so the blob test can construct a historical blob and prove
    /// the v2 reader rejects it, rather than asserting rejection against a
    /// hand-rolled byte pattern that could drift from the real v1 layout.
    static uint32_t* paramsIn(uint8_t* blob) { return reinterpret_cast<uint32_t*>(blob + sizeof(uint32_t) * 2); }

    static const uint32_t* paramsIn(const uint8_t* blob) {
        return reinterpret_cast<const uint32_t*>(blob + sizeof(uint32_t) * 2);
    }

    const ParamSpec* findSpec(uint32_t id) const AESTRA_RT_NONBLOCKING {
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            if (p->id == id)
                return p;
        }
        return nullptr;
    }

    const ParamSpec* findBypassSpec() const AESTRA_RT_NONBLOCKING {
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            if (p->isBypass)
                return p;
        }
        return nullptr;
    }

    std::array<std::atomic<float>, kMaxSpecParams> m_params{};
    std::atomic<bool> m_defaultsSeeded{false};
};

} // namespace Audio
} // namespace Aestra
