// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "NUICoreWidgets.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace NomadUI {

class PlayButton : public NUIComponent {
public:
    PlayButton();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setPlaying(bool playing);
    bool isPlaying() const { return playing_; }

    void setOnToggle(std::function<void(bool)> callback);

private:
    bool playing_;
    std::function<void(bool)> onToggle_;
};

class StopButton : public NUIComponent {
public:
    StopButton();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setOnStop(std::function<void()> callback);

private:
    std::function<void()> onStop_;
};

class RecordButton : public NUIComponent {
public:
    RecordButton();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setArmed(bool armed);
    bool isArmed() const { return armed_; }

    void setOnToggle(std::function<void(bool)> callback);

private:
    bool armed_;
    std::function<void(bool)> onToggle_;
};

class LoopToggle : public NUIToggle {
public:
    LoopToggle();
};

class RewindButton : public NUIComponent {
public:
    RewindButton();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setOnRewind(std::function<void()> callback);

private:
    std::function<void()> onRewind_;
};

class ForwardButton : public NUIComponent {
public:
    ForwardButton();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setOnForward(std::function<void()> callback);

private:
    std::function<void()> onForward_;
};

class TempoDisplay : public NUIComponent {
public:
    TempoDisplay();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setTempo(double bpm);
    double getTempo() const { return tempo_; }

    void setOnTempoChanged(std::function<void(double)> callback);

private:
    double tempo_;
    std::function<void(double)> onTempoChanged_;
};

class TimeSignatureDisplay : public NUIComponent {
public:
    TimeSignatureDisplay();

    void onRender(NUIRenderer& renderer) override;

    void setNumerator(int numerator);
    void setDenominator(int denominator);

    int getNumerator() const { return numerator_; }
    int getDenominator() const { return denominator_; }

private:
    int numerator_;
    int denominator_;
};

class ClockDisplay : public NUIComponent {
public:
    ClockDisplay();

    void onRender(NUIRenderer& renderer) override;

    void setTimeString(const std::string& value);
    const std::string& getTimeString() const { return timeString_; }

private:
    std::string timeString_;
};

class MasterVU : public NUIMeter {
public:
    MasterVU();
};

class CPUIndicator : public NUIComponent {
public:
    CPUIndicator();

    void onRender(NUIRenderer& renderer) override;

    void setLoad(float load);
    float getLoad() const { return load_; }

private:
    float load_;
};

class TransportBar : public NUIComponent {
public:
    TransportBar();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;

    PlayButton& getPlayButton() { return *playButton_; }
    StopButton& getStopButton() { return *stopButton_; }
    RecordButton& getRecordButton() { return *recordButton_; }
    LoopToggle& getLoopToggle() { return *loopToggle_; }
    RewindButton& getRewindButton() { return *rewindButton_; }
    ForwardButton& getForwardButton() { return *forwardButton_; }
    TempoDisplay& getTempoDisplay() { return *tempoDisplay_; }
    TimeSignatureDisplay& getTimeSignatureDisplay() { return *timeSignatureDisplay_; }
    ClockDisplay& getClockDisplay() { return *clockDisplay_; }
    MasterVU& getMasterVU() { return *masterVU_; }
    CPUIndicator& getCPUIndicator() { return *cpuIndicator_; }

private:
    std::shared_ptr<PlayButton> playButton_;
    std::shared_ptr<StopButton> stopButton_;
    std::shared_ptr<RecordButton> recordButton_;
    std::shared_ptr<LoopToggle> loopToggle_;
    std::shared_ptr<RewindButton> rewindButton_;
    std::shared_ptr<ForwardButton> forwardButton_;
    std::shared_ptr<TempoDisplay> tempoDisplay_;
    std::shared_ptr<TimeSignatureDisplay> timeSignatureDisplay_;
    std::shared_ptr<ClockDisplay> clockDisplay_;
    std::shared_ptr<MasterVU> masterVU_;
    std::shared_ptr<CPUIndicator> cpuIndicator_;
};

} // namespace NomadUI

