// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUISlider.h"
#include "NUICoreWidgets.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace NomadUI {

class Fader : public NUISlider {
public:
    Fader();
};

class PanKnob : public NUISlider {
public:
    PanKnob();
};

class TrackLabel : public NUIComponent {
public:
    TrackLabel();

    void onRender(NUIRenderer& renderer) override;

    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void setColor(const NUIColor& color);
    NUIColor getColor() const { return color_; }

private:
    std::string text_;
    NUIColor color_;
};

class MuteButton : public NUIToggle {
public:
    MuteButton();
};

class SoloButton : public NUIToggle {
public:
    SoloButton();
};

class ArmButton : public NUIToggle {
public:
    ArmButton();
};

class InsertSlot : public NUIComponent {
public:
    InsertSlot();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setPluginName(const std::string& name);
    const std::string& getPluginName() const { return pluginName_; }

    void setOnActivate(std::function<void()> callback);

private:
    std::string pluginName_;
    std::function<void()> onActivate_;
};

class SendSlot : public NUIComponent {
public:
    SendSlot();

    void onRender(NUIRenderer& renderer) override;

    void setAmount(float amount);
    float getAmount() const { return amount_; }

private:
    float amount_;
};

class MeterStrip : public NUIMeter {
public:
    MeterStrip();
};

class ChannelStrip : public NUIComponent {
public:
    ChannelStrip();

    void onRender(NUIRenderer& renderer) override;

    Fader& getFader() { return *fader_; }
    PanKnob& getPanKnob() { return *panKnob_; }
    TrackLabel& getTrackLabel() { return *trackLabel_; }
    MuteButton& getMuteButton() { return *muteButton_; }
    SoloButton& getSoloButton() { return *soloButton_; }
    ArmButton& getArmButton() { return *armButton_; }
    MeterStrip& getMeterStrip() { return *meterStrip_; }

    std::vector<std::shared_ptr<InsertSlot>>& getInserts() { return inserts_; }
    std::vector<std::shared_ptr<SendSlot>>& getSends() { return sends_; }

    void addInsert();
    void addSend();

private:
    std::shared_ptr<Fader> fader_;
    std::shared_ptr<PanKnob> panKnob_;
    std::shared_ptr<TrackLabel> trackLabel_;
    std::shared_ptr<MuteButton> muteButton_;
    std::shared_ptr<SoloButton> soloButton_;
    std::shared_ptr<ArmButton> armButton_;
    std::shared_ptr<MeterStrip> meterStrip_;
    std::vector<std::shared_ptr<InsertSlot>> inserts_;
    std::vector<std::shared_ptr<SendSlot>> sends_;
};

class MixerPanel : public NUIComponent {
public:
    MixerPanel();

    void onRender(NUIRenderer& renderer) override;

    void addChannelStrip(std::shared_ptr<ChannelStrip> strip);
    const std::vector<std::shared_ptr<ChannelStrip>>& getChannelStrips() const { return channels_; }

private:
    std::vector<std::shared_ptr<ChannelStrip>> channels_;
};

} // namespace NomadUI

