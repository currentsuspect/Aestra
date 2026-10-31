// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-H4/H5: how parameter values travel between the host, a VST3 controller (the half with the editor) and its
// processor (the half that makes sound). VST3 keeps them apart on purpose: setting a value on the controller does not
// reach the processor, so a host that only calls IEditController::setParamNormalized() hears nothing change. The values
// have to be handed to process() as IParameterChanges, and anything the plugin's own window changes arrives through
// IComponentHandler::performEdit and has to take the same road.
//
//  * ParamBridge          values waiting to go to the processor (any thread pushes; the audio thread drains, never blocks)
//  * HostParameterChanges the pending values as the IParameterChanges handed to process(), fixed capacity, no allocation
//  * OutputParameterChanges what the processor reports back (meters, its own automation), read by the host after process()
//  * HostComponentHandler the controller's side: edits made in the plugin window become pending values

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

constexpr size_t kMaxParamChangesPerBlock = 128;

/// One value for one parameter, normalized 0..1.
struct ParamChange {
    uint32_t id = 0;
    double value = 0.0;
};

class ParamBridge {
public:
    /// Queue a value for the processor. A newer value for the same parameter replaces the waiting one (only the latest
    /// matters inside one block). Called from the UI, the plugin's window thread, or the helper's command thread.
    void push(uint32_t id, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pending : m_pending) {
            if (pending.id == id) {
                pending.value = value;
                return;
            }
        }
        m_pending.push_back({id, value});
    }

    /// Take up to @p max waiting values into @p out for this block; the audio thread never waits for the lock (if a
    /// producer holds it the values simply go in the next block). Values beyond @p max stay for the next block.
    size_t drain(ParamChange* out, size_t max) {
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock() || m_pending.empty()) return 0;
        const size_t n = std::min(max, m_pending.size());
        for (size_t i = 0; i < n; ++i) out[i] = m_pending[i];
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<ParamChange> m_pending;
};

namespace detail {
/// A queue with the one point a host ever needs to send (or the last the plugin sent).
class SinglePointQueue final : public Steinberg::Vst::IParamValueQueue {
public:
    Steinberg::Vst::ParamID id = 0;
    Steinberg::int32 offset = 0;
    Steinberg::Vst::ParamValue value = 0.0;

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& v) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = offset;
        v = value;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue v,
                                           Steinberg::int32& index) override {
        offset = sampleOffset; // keeps only the newest point: enough for what the host gets back
        value = v;
        index = 0;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }  // owned by the list, which lives on the stack
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};
} // namespace detail

/// The values the host sends the processor this block.
class HostParameterChanges final : public Steinberg::Vst::IParameterChanges {
public:
    void fill(const ParamChange* values, size_t n) {
        m_count = static_cast<Steinberg::int32>(std::min(n, m_queues.size()));
        for (Steinberg::int32 i = 0; i < m_count; ++i) {
            m_queues[static_cast<size_t>(i)].id = values[i].id;
            m_queues[static_cast<size_t>(i)].offset = 0;
            m_queues[static_cast<size_t>(i)].value = values[i].value;
        }
    }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return m_count; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        return (index >= 0 && index < m_count) ? &m_queues[static_cast<size_t>(index)] : nullptr;
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID&,
                                                                  Steinberg::int32& index) override {
        index = -1; // the host fills this one; the plugin has no business adding to it
        return nullptr;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
    std::array<detail::SinglePointQueue, kMaxParamChangesPerBlock> m_queues;
    Steinberg::int32 m_count = 0;
};

/// What the processor reports during process(): its own parameter moves, for the controller (and so the window) to show.
class OutputParameterChanges final : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::int32 PLUGIN_API getParameterCount() override { return m_count; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        return (index >= 0 && index < m_count) ? &m_queues[static_cast<size_t>(index)] : nullptr;
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                  Steinberg::int32& index) override {
        for (Steinberg::int32 i = 0; i < m_count; ++i) {
            if (m_queues[static_cast<size_t>(i)].id == id) {
                index = i;
                return &m_queues[static_cast<size_t>(i)];
            }
        }
        if (m_count >= static_cast<Steinberg::int32>(m_queues.size())) return nullptr;
        index = m_count++;
        m_queues[static_cast<size_t>(index)].id = id;
        return &m_queues[static_cast<size_t>(index)];
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    void clear() { m_count = 0; }
    /// (id, last value) pairs the plugin reported.
    size_t collect(ParamChange* out, size_t max) const {
        const size_t n = std::min(max, static_cast<size_t>(m_count));
        for (size_t i = 0; i < n; ++i) out[i] = {m_queues[i].id, m_queues[i].value};
        return n;
    }

private:
    std::array<detail::SinglePointQueue, kMaxParamChangesPerBlock> m_queues;
    Steinberg::int32 m_count = 0;
};

/// The controller's host: what the plugin's own window tells us. performEdit() is the user moving a knob: it goes to
/// the processor through the bridge, and to whoever wants to hear about it (the app, for automation recording).
class HostComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    using EditCallback = std::function<void(uint32_t id, double normalized)>;

    HostComponentHandler(ParamBridge& bridge, EditCallback onEdit) : m_bridge(bridge), m_onEdit(std::move(onEdit)) {}

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) override {
        m_bridge.push(id, value);
        if (m_onEdit) m_onEdit(id, value);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        m_restartFlags.fetch_or(flags, std::memory_order_acq_rel);
        return Steinberg::kResultOk;
    }
    /// Flags a plugin raised through restartComponent() since the last call (kLatencyChanged, kParamValuesChanged...).
    Steinberg::int32 takeRestartFlags() { return m_restartFlags.exchange(0, std::memory_order_acq_rel); }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (!obj) return Steinberg::kInvalidArgument;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; } // owned by the plugin instance
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
    ParamBridge& m_bridge;
    EditCallback m_onEdit;
    std::atomic<Steinberg::int32> m_restartFlags{0};
};

} // namespace Audio
} // namespace Aestra
