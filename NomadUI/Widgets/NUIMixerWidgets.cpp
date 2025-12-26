// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIMixerWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

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


UIMixerSend::UIMixerSend()
{
    destSelector_ = std::make_shared<UIItemSelector>();
    
    // Forward selection changes
    destSelector_->setOnSelectionChanged([this](int index) {
        if (onDestChanged_ && index >= 0 && index < static_cast<int>(destinations_.size())) {
            onDestChanged_(destinations_[index].first);
        }
    });

    levelKnob_ = std::make_shared<UIMixerKnob>(UIMixerKnobType::Send);
    levelKnob_->setValue(0.7f); // Unity-ish
    levelKnob_->onValueChanged = [this](float v) {
        if (onLevelChanged_) onLevelChanged_(v);
    };

    deleteButton_ = std::make_shared<NUIButton>("");
    deleteButton_->setStyle(NUIButton::Style::Secondary); // Visible border/bg
    
    auto trashIcon = NUIIcon::createTrashIcon();
    trashIcon->setIconSize(14, 14); 
    trashIcon->setBounds({3, 3, 14, 14});
    trashIcon->setColor(NUIColor::white()); // Force white icon
    
    deleteButton_->addChild(trashIcon);
    // Red-ish background for visibility/danger
    deleteButton_->setBackgroundColor(NUIColor::fromHex(0x502020)); 
    deleteButton_->setBorderEnabled(true);

    deleteButton_->setOnClick([this]() {
        if (onDelete_) onDelete_();
    });

    addChild(destSelector_);
    addChild(levelKnob_);
    addChild(deleteButton_);
}

void UIMixerSend::onRender(NUIRenderer& renderer)
{
    // Layout: Knob on left, Selector on right
    auto b = getBounds();
    const float knobSize = b.height - 4.0f;
    
    NUIRect knobRect = {b.x + 2.0f, b.y + 2.0f, knobSize, knobSize};
    
    float comboX = b.x + knobSize + 8.0f;
    float deleteBtnSize = 20.0f;
    float maxComboWidth = 120.0f;
    float availableWidth = b.width - (knobSize + 10.0f) - (deleteBtnSize + 4.0f); // Reserve space for delete button
    float comboWidth = (availableWidth > maxComboWidth) ? maxComboWidth : availableWidth;
    
    NUIRect comboRect = {comboX, b.y + 2.0f, comboWidth, b.height - 4.0f};
    // Ensure button has integer coordinates for crisp rendering
    float delX = std::floor(comboX + comboWidth + 4.0f);
    float delY = std::floor(b.y + (b.height - deleteBtnSize) * 0.5f);
    NUIRect deleteRect = {delX, delY, deleteBtnSize, deleteBtnSize};

    levelKnob_->setBounds(knobRect);
    destSelector_->setBounds(comboRect);
    deleteButton_->setBounds(deleteRect);

    renderChildren(renderer);
}

void UIMixerSend::setDestination(uint32_t destId, const std::string& name)
{
    // Find index for destId
    for (size_t i = 0; i < destinations_.size(); ++i) {
        if (destinations_[i].first == destId) {
            destSelector_->setSelectedIndex(static_cast<int>(i));
            return;
        }
    }
}

uint32_t UIMixerSend::getDestinationId() const
{
    int idx = destSelector_->getSelectedIndex();
    if (idx >= 0 && idx < static_cast<int>(destinations_.size())) {
        return destinations_[idx].first;
    }
    return 0; // Default to 0? Or maybe verify valid?
}

void UIMixerSend::setLevel(float level)
{
    levelKnob_->setValue(level);
}

float UIMixerSend::getLevel() const
{
    return levelKnob_->getValue();
}

void UIMixerSend::setAvailableDestinations(const std::vector<std::pair<uint32_t, std::string>>& dests)
{
    destinations_ = dests;
    std::vector<std::string> items;
    items.reserve(dests.size());
    for (const auto& p : dests) {
        items.push_back(p.second);
    }
    destSelector_->setItems(items);
}

void UIMixerSend::setOnDestinationChanged(std::function<void(uint32_t)> cb)
{
    onDestChanged_ = std::move(cb);
}

void UIMixerSend::setOnLevelChanged(std::function<void(float)> cb)
{
    onLevelChanged_ = std::move(cb);
}

void UIMixerSend::setOnDelete(std::function<void()> cb)
{
    onDelete_ = std::move(cb);
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
    auto slot = std::make_shared<UIMixerSend>();
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

