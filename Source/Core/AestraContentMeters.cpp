// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The transport bar's DSP load (DspLoadReadout): the last audio callback's time over its buffer's duration, read
// from the engine's telemetry each UI frame. Relaxed atomic loads only; the audio thread writes them.
#include "MidiInputService.h"
#include "AestraContent.h"
#include "AudioEngine.h"
#include "TransportBar.h"

void AestraContent::updateDspLoadReadout(double dt) {
    if (!m_transportBar)
        return;
    if (!m_audioEngine) {
        m_transportBar->setDspLoad(-1.0f);
        return;
    }
    auto& telemetry = m_audioEngine->telemetry();
    const float percent = Aestra::DspLoadReadout::percentOf(telemetry.lastCallbackNs.load(std::memory_order_relaxed),
                                                            telemetry.getCallbackBudgetNs());
    m_transportBar->setDspLoad(m_dspLoad.update(percent, dt));
}

// #456: MidiInputService::refreshPorts() re-scans and opens newly connected ports, but nothing called it, so a
// controller plugged in after startup was never picked up. The UI thread is a control thread, so it polls here, at a
// low rate: a rescan enumerates the system's MIDI ports, which is cheap but not free.
void AestraContent::updateFrameServices(double dt) {
    updateDspLoadReadout(dt);
    constexpr double kMidiPortPollSeconds = 2.0;
    m_midiPortPollSeconds += dt;
    if (m_midiPortPollSeconds >= kMidiPortPollSeconds) {
        m_midiPortPollSeconds = 0.0;
        if (m_midiInput) m_midiInput->refreshPorts();
    }
}
