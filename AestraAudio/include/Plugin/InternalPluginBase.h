// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

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
/// State compatibility (AGENTS.md §11): the blob is the same {magic, version,
/// params[]} layout plugins already ship, so migrating a plugin does not
/// invalidate a saved project. A plugin whose blob carries extra fields
/// overrides saveState()/loadState() instead.
class InternalPluginBase : public IPluginInstance {
public:
    ~InternalPluginBase() override = default;

    // ==================================================================
    // Subclass contract
    // ==================================================================

    /// The plugin's parameter table. Must outlive the instance.
    virtual const ParamSpec* paramSpecs() const = 0;

    /// Number of rows in paramSpecs().
    virtual uint32_t paramSpecCount() const = 0;

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
    virtual void onParameterChanged(uint32_t /*id*/, float /*value*/) {}

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

    float getParameter(uint32_t id) const override {
        if (id >= kMaxSpecParams)
            return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    /// A non-finite value is rejected outright, leaving the previous value in
    /// place. std::clamp(NaN, 0, 1) is NaN, so a clamp alone does not stop
    /// one, and a NaN parameter poisons the DSP for the life of the instance.
    void setParameter(uint32_t id, float value) override {
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

    /// The blob is {magic, version, params[paramSpecCount()]} — exactly the
    /// layout each plugin shipped before it inherited this, so migrating does
    /// not change a single byte of saved project state. The array is sized by
    /// the plugin's own count rather than by kMaxSpecParams: reserving the
    /// ceiling here would inflate every migrated plugin's blob (Transient's
    /// 28 bytes would become 200) and silently invalidate every project that
    /// already has one.
    std::vector<uint8_t> saveState() const override {
        const uint32_t count = paramSpecCount();
        std::vector<uint8_t> blob(blobSize(count));
        writeHeader(blob.data(), stateMagic(), kStateVersion);
        float* params = paramsIn(blob.data());
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            const ParamSpec& spec = *p;
            params[spec.id] = m_params[spec.id].load(std::memory_order_relaxed);
        }
        return blob;
    }

    /// Rejects the whole blob before touching any parameter (AGENTS.md §12):
    /// a corrupt blob must not leave the instance half-updated.
    bool loadState(const std::vector<uint8_t>& state) override {
        const uint32_t count = paramSpecCount();
        if (state.size() < blobSize(count))
            return false;
        if (!headerMatches(state.data(), stateMagic(), kStateVersion))
            return false;

        std::vector<float> loaded(count, 0.0f);
        std::memcpy(loaded.data(), paramsIn(state.data()), sizeof(float) * count);

        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            const ParamSpec& spec = *p;
            const float value = loaded[spec.id];
            if (!std::isfinite(value) || value < spec.minValue || value > spec.maxValue)
                return false;
        }
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            const ParamSpec& spec = *p;
            const float value = loaded[spec.id];
            m_params[spec.id].store(value, std::memory_order_relaxed);
            onParameterChanged(spec.id, value);
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
    static constexpr uint32_t kMaxSpecParams = 48;
    static constexpr uint32_t kStateVersion = 1;

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
    float paramValue(uint32_t id) const { return m_params[id].load(std::memory_order_relaxed); }

    /// The bypass knob, read the way every plugin reads it.
    bool isBypassed() const {
        const ParamSpec* spec = findBypassSpec();
        return spec ? m_params[spec->id].load(std::memory_order_relaxed) > 0.5f : false;
    }

private:
    const ParamSpec* specsBegin() const { return paramSpecs(); }
    const ParamSpec* specsEnd() const { return paramSpecs() + paramSpecCount(); }

    static constexpr size_t blobSize(uint32_t count) { return sizeof(uint32_t) * 2 + sizeof(float) * count; }

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

    static float* paramsIn(uint8_t* blob) { return reinterpret_cast<float*>(blob + sizeof(uint32_t) * 2); }

    static const float* paramsIn(const uint8_t* blob) {
        return reinterpret_cast<const float*>(blob + sizeof(uint32_t) * 2);
    }

    const ParamSpec* findSpec(uint32_t id) const {
        for (const ParamSpec* p = specsBegin(); p != specsEnd(); ++p) {
            if (p->id == id)
                return p;
        }
        return nullptr;
    }

    const ParamSpec* findBypassSpec() const {
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
