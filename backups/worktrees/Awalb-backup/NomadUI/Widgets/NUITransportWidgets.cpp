// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITransportWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include <algorithm>

namespace NomadUI {

PlayButton::PlayButton()
    : playing_(false)
{
}

void PlayButton::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool PlayButton::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        playing_ = !playing_;
        repaint();
        if (onToggle_)
            onToggle_(playing_);
        return true;
    }
    return false;
}

void PlayButton::setPlaying(bool playing)
{
    if (playing_ == playing)
        return;
    playing_ = playing;
    repaint();
}

void PlayButton::setOnToggle(std::function<void(bool)> callback)
{
    onToggle_ = std::move(callback);
}

StopButton::StopButton() = default;

void StopButton::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool StopButton::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        if (onStop_)
            onStop_();
        return true;
    }
    return false;
}

void StopButton::setOnStop(std::function<void()> callback)
{
    onStop_ = std::move(callback);
}

RecordButton::RecordButton()
    : armed_(false)
{
}

void RecordButton::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool RecordButton::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        armed_ = !armed_;
        repaint();
        if (onToggle_)
            onToggle_(armed_);
        return true;
    }
    return false;
}

void RecordButton::setArmed(bool armed)
{
    if (armed_ == armed)
        return;
    armed_ = armed;
    repaint();
}

void RecordButton::setOnToggle(std::function<void(bool)> callback)
{
    onToggle_ = std::move(callback);
}

LoopToggle::LoopToggle()
{
    setOn(false);
}

RewindButton::RewindButton() = default;

void RewindButton::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool RewindButton::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        if (onRewind_)
            onRewind_();
        return true;
    }
    return false;
}

void RewindButton::setOnRewind(std::function<void()> callback)
{
    onRewind_ = std::move(callback);
}

ForwardButton::ForwardButton() = default;

void ForwardButton::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool ForwardButton::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        if (onForward_)
            onForward_();
        return true;
    }
    return false;
}

void ForwardButton::setOnForward(std::function<void()> callback)
{
    onForward_ = std::move(callback);
}

TempoDisplay::TempoDisplay()
    : tempo_(120.0)
{
}

void TempoDisplay::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool TempoDisplay::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.wheelDelta != 0.0f)
    {
        setTempo(tempo_ + event.wheelDelta);
        return true;
    }
    return false;
}

void TempoDisplay::setTempo(double bpm)
{
    bpm = std::clamp(bpm, 20.0, 999.0);
    if (tempo_ == bpm)
        return;
    tempo_ = bpm;
    repaint();
    if (onTempoChanged_)
        onTempoChanged_(tempo_);
}

void TempoDisplay::setOnTempoChanged(std::function<void(double)> callback)
{
    onTempoChanged_ = std::move(callback);
}

TimeSignatureDisplay::TimeSignatureDisplay()
    : numerator_(4), denominator_(4)
{
}

void TimeSignatureDisplay::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void TimeSignatureDisplay::setNumerator(int numerator)
{
    numerator_ = std::max(1, numerator);
    repaint();
}

void TimeSignatureDisplay::setDenominator(int denominator)
{
    static const int allowed[] = {1, 2, 4, 8, 16, 32};
    const int* it = std::find(std::begin(allowed), std::end(allowed), denominator);
    denominator_ = it != std::end(allowed) ? *it : 4;
    repaint();
}

ClockDisplay::ClockDisplay() = default;

void ClockDisplay::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ClockDisplay::setTimeString(const std::string& value)
{
    timeString_ = value;
    repaint();
}

MasterVU::MasterVU()
{
    setChannelCount(2);
}

CPUIndicator::CPUIndicator()
    : load_(0.0f)
{
}

void CPUIndicator::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void CPUIndicator::setLoad(float load)
{
    load_ = std::clamp(load, 0.0f, 1.0f);
    repaint();
}

TransportBar::TransportBar()
{
    playButton_ = std::make_shared<PlayButton>();
    stopButton_ = std::make_shared<StopButton>();
    recordButton_ = std::make_shared<RecordButton>();
    loopToggle_ = std::make_shared<LoopToggle>();
    rewindButton_ = std::make_shared<RewindButton>();
    forwardButton_ = std::make_shared<ForwardButton>();
    tempoDisplay_ = std::make_shared<TempoDisplay>();
    timeSignatureDisplay_ = std::make_shared<TimeSignatureDisplay>();
    clockDisplay_ = std::make_shared<ClockDisplay>();
    masterVU_ = std::make_shared<MasterVU>();
    cpuIndicator_ = std::make_shared<CPUIndicator>();

    addChild(playButton_);
    addChild(stopButton_);
    addChild(recordButton_);
    addChild(loopToggle_);
    addChild(rewindButton_);
    addChild(forwardButton_);
    addChild(tempoDisplay_);
    addChild(timeSignatureDisplay_);
    addChild(clockDisplay_);
    addChild(masterVU_);
    addChild(cpuIndicator_);
}

void TransportBar::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void TransportBar::onUpdate(double deltaTime)
{
    (void)deltaTime;
}

} // namespace NomadUI

