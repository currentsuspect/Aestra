// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The transport bar's DSP load (DspLoadReadout): the last audio callback's time over its buffer's duration, read
// from the engine's telemetry each UI frame. Relaxed atomic loads only; the audio thread writes them.
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
