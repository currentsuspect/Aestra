// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "NUICoreWidgets.h"
#include <vector>

namespace NomadUI {

class AudioVisualizer : public NUIComponent {
public:
    AudioVisualizer();

    void onRender(NUIRenderer& renderer) override;
    void setWaveformData(const std::vector<float>& data);

private:
    std::vector<float> waveform_;
};

class SpectrumAnalyzer : public NUIComponent {
public:
    SpectrumAnalyzer();

    void onRender(NUIRenderer& renderer) override;
    void setSpectrumData(const std::vector<float>& data);

private:
    std::vector<float> spectrum_;
};

class PhaseScope : public NUIComponent {
public:
    PhaseScope();

    void onRender(NUIRenderer& renderer) override;
    void setPhaseData(const std::vector<NUIPoint>& points);

private:
    std::vector<NUIPoint> points_;
};

class WaveformDisplay : public NUIComponent {
public:
    WaveformDisplay();

    void onRender(NUIRenderer& renderer) override;
    void setWaveform(const std::vector<float>& data);

private:
    std::vector<float> waveform_;
};

class VUBridge : public NUIComponent {
public:
    VUBridge();

    void onRender(NUIRenderer& renderer) override;

    void setLeftLevel(float value);
    void setRightLevel(float value);

    float getLeftLevel() const { return leftLevel_; }
    float getRightLevel() const { return rightLevel_; }

private:
    float leftLevel_;
    float rightLevel_;
};

} // namespace NomadUI

