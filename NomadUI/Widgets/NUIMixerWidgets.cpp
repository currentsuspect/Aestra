// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIMixerWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <algorithm>

namespace NomadUI {

Fader::Fader()
{
    setOrientation(NUISlider::Orientation::Vertical);
}

PanKnob::PanKnob()
{
    setStyle(NUISlider::Style::Rotary);
    setRange(-1.0f, 1.0f);
    setValue(0.0f);
}

TrackLabel::TrackLabel()
    : text_("Track"), color_(NUIColor::fromHex(0xff6633ff))
{
}

void TrackLabel::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void TrackLabel::setText(const std::string& text)
{
    text_ = text;
    repaint();
}

void TrackLabel::setColor(const NUIColor& color)
{
    color_ = color;
    repaint();
}

MuteButton::MuteButton()
{
    setOn(false);
}

void MuteButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    NUIColor bg = isOn() ? NUIColor(0.8f, 0.2f, 0.2f, 1.0f) : theme.getColor("backgroundSecondary");
    NUIColor text = isOn() ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : theme.getColor("textPrimary");
    
    renderer.fillRect(b, bg);
    renderer.strokeRect(b, 1, theme.getColor("border"));
    renderer.drawTextCentered("M", b, 13.0f, text);
}

SoloButton::SoloButton()
{
    setOn(false);
}

void SoloButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    NUIColor bg = isOn() ? NUIColor(0.9f, 0.8f, 0.2f, 1.0f) : theme.getColor("backgroundSecondary");
    NUIColor text = isOn() ? NUIColor(0.0f, 0.0f, 0.0f, 1.0f) : theme.getColor("textPrimary");
    
    renderer.fillRect(b, bg);
    renderer.strokeRect(b, 1, theme.getColor("border"));
    renderer.drawTextCentered("S", b, 13.0f, text);
}

ArmButton::ArmButton()
{
    setOn(false);
}

void ArmButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    NUIColor bg = isOn() ? NUIColor(0.8f, 0.2f, 0.2f, 1.0f) : theme.getColor("backgroundSecondary");
    NUIColor text = isOn() ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f) : theme.getColor("textPrimary");
    
    renderer.fillRect(b, bg);
    renderer.strokeRect(b, 1, theme.getColor("border"));
    renderer.drawTextCentered("R", b, 13.0f, text);
}

InsertSlot::InsertSlot() = default;

void InsertSlot::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool InsertSlot::onMouseEvent(const NUIMouseEvent& event)
{
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        if (onActivate_)
            onActivate_();
        return true;
    }
    return false;
}

void InsertSlot::setPluginName(const std::string& name)
{
    pluginName_ = name;
    repaint();
}

void InsertSlot::setOnActivate(std::function<void()> callback)
{
    onActivate_ = std::move(callback);
}

SendSlot::SendSlot()
    : amount_(0.0f)
{
}

void SendSlot::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void SendSlot::setAmount(float amount)
{
    amount_ = std::clamp(amount, 0.0f, 1.0f);
    repaint();
}

MeterStrip::MeterStrip()
{
    setChannelCount(2);
}

ChannelStrip::ChannelStrip()
{
    fader_ = std::make_shared<Fader>();
    panKnob_ = std::make_shared<PanKnob>();
    trackLabel_ = std::make_shared<TrackLabel>();
    muteButton_ = std::make_shared<MuteButton>();
    soloButton_ = std::make_shared<SoloButton>();
    armButton_ = std::make_shared<ArmButton>();
    meterStrip_ = std::make_shared<MeterStrip>();

    addChild(fader_);
    addChild(panKnob_);
    addChild(trackLabel_);
    addChild(muteButton_);
    addChild(soloButton_);
    addChild(armButton_);
    addChild(meterStrip_);
}

void ChannelStrip::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ChannelStrip::addInsert()
{
    auto slot = std::make_shared<InsertSlot>();
    inserts_.push_back(slot);
    addChild(slot);
}

void ChannelStrip::addSend()
{
    auto slot = std::make_shared<SendSlot>();
    sends_.push_back(slot);
    addChild(slot);
}

MixerPanel::MixerPanel() = default;

void MixerPanel::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void MixerPanel::addChannelStrip(std::shared_ptr<ChannelStrip> strip)
{
    if (!strip)
        return;
    channels_.push_back(strip);
    addChild(strip);
}

} // namespace NomadUI

