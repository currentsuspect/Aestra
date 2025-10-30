// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIVisualWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include <algorithm>

namespace NomadUI {

AudioVisualizer::AudioVisualizer() = default;

void AudioVisualizer::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void AudioVisualizer::setWaveformData(const std::vector<float>& data)
{
    waveform_ = data;
    repaint();
}

SpectrumAnalyzer::SpectrumAnalyzer() = default;

void SpectrumAnalyzer::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void SpectrumAnalyzer::setSpectrumData(const std::vector<float>& data)
{
    spectrum_ = data;
    repaint();
}

PhaseScope::PhaseScope() = default;

void PhaseScope::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void PhaseScope::setPhaseData(const std::vector<NUIPoint>& points)
{
    points_ = points;
    repaint();
}

WaveformDisplay::WaveformDisplay() = default;

void WaveformDisplay::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void WaveformDisplay::setWaveform(const std::vector<float>& data)
{
    waveform_ = data;
    repaint();
}

VUBridge::VUBridge()
    : leftLevel_(0.0f), rightLevel_(0.0f)
{
}

void VUBridge::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void VUBridge::setLeftLevel(float value)
{
    leftLevel_ = std::clamp(value, 0.0f, 1.0f);
    repaint();
}

void VUBridge::setRightLevel(float value)
{
    rightLevel_ = std::clamp(value, 0.0f, 1.0f);
    repaint();
}

} // namespace NomadUI

