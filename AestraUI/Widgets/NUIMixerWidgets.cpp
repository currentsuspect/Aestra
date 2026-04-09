// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIMixerWidgets.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

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
    NUIColor base = theme.getColor("warning");
    NUIColor bg = theme.getColor("buttonBgDefault").withAlpha(0.98f);
    NUIColor border = theme.getColor("border").withAlpha(0.28f);
    NUIColor text = theme.getColor("textSecondary").withAlpha(0.86f);

    if (isOn()) {
        bg = theme.getColor("buttonBgActive").withAlpha(0.99f);
        border = base.withAlpha(0.34f);
        text = theme.getColor("textPrimary");
    } else if (isHovered()) {
        bg = theme.getColor("buttonBgHover").withAlpha(0.99f);
        border = theme.getColor("border").withAlpha(0.38f);
        text = theme.getColor("textPrimary").withAlpha(0.92f);
    }

    renderer.drawShadow(b, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.12f));
    renderer.fillRoundedRect(b, 7.0f, bg);
    renderer.strokeRoundedRect(b, 7.0f, 1.0f, border);
    renderer.strokeRoundedRect({b.x + 1.0f, b.y + 1.0f, b.width - 2.0f, b.height - 2.0f},
                               6.0f,
                               1.0f,
                               NUIColor::white().withAlpha(0.025f));
    renderer.drawTextCentered("M", b, 12.0f, text);
}

SoloButton::SoloButton()
{
    setOn(false);
}

void SoloButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    NUIColor base = theme.getColor("accentCyan");
    NUIColor bg = theme.getColor("buttonBgDefault").withAlpha(0.98f);
    NUIColor border = theme.getColor("border").withAlpha(0.28f);
    NUIColor text = theme.getColor("textSecondary").withAlpha(0.86f);

    if (isOn()) {
        bg = theme.getColor("buttonBgActive").withAlpha(0.99f);
        border = base.withAlpha(0.32f);
        text = theme.getColor("textPrimary");
    } else if (isHovered()) {
        bg = theme.getColor("buttonBgHover").withAlpha(0.99f);
        border = theme.getColor("border").withAlpha(0.38f);
        text = theme.getColor("textPrimary").withAlpha(0.92f);
    }

    renderer.drawShadow(b, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.12f));
    renderer.fillRoundedRect(b, 7.0f, bg);
    renderer.strokeRoundedRect(b, 7.0f, 1.0f, border);
    renderer.strokeRoundedRect({b.x + 1.0f, b.y + 1.0f, b.width - 2.0f, b.height - 2.0f},
                               6.0f,
                               1.0f,
                               NUIColor::white().withAlpha(0.025f));
    renderer.drawTextCentered("S", b, 12.0f, text);
}

ArmButton::ArmButton()
{
    setOn(false);
}

void ArmButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    NUIColor base = theme.getColor("error");
    NUIColor bg = theme.getColor("buttonBgDefault").withAlpha(0.98f);
    NUIColor border = theme.getColor("border").withAlpha(0.28f);
    NUIColor text = theme.getColor("textSecondary").withAlpha(0.86f);

    if (isOn()) {
        bg = theme.getColor("buttonBgActive").withAlpha(0.99f);
        border = base.withAlpha(0.34f);
        text = theme.getColor("textPrimary");
    } else if (isHovered()) {
        bg = theme.getColor("buttonBgHover").withAlpha(0.99f);
        border = theme.getColor("border").withAlpha(0.38f);
        text = theme.getColor("textPrimary").withAlpha(0.92f);
    }

    renderer.drawShadow(b, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.12f));
    renderer.fillRoundedRect(b, 7.0f, bg);
    renderer.strokeRoundedRect(b, 7.0f, 1.0f, border);
    renderer.strokeRoundedRect({b.x + 1.0f, b.y + 1.0f, b.width - 2.0f, b.height - 2.0f},
                               6.0f,
                               1.0f,
                               NUIColor::white().withAlpha(0.025f));
    
    // Circle or icon for Record? Keeping "R" for consistency but could be circle
    // Let's use a small filled circle for R to look like a rec light
    if (isOn()) {
        float cx = b.x + b.width * 0.5f;
        float cy = b.y + b.height * 0.5f;
        renderer.fillCircle({cx, cy}, 3.5f, base.withAlpha(0.92f));
    } else {
        renderer.drawTextCentered("R", b, 12.0f, text);
    }
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
    : m_accentColor(NUIThemeManager::getInstance().getColor("accentPrimary"))
{
    destSelector_ = std::make_shared<UIItemSelector>();
    destSelector_->setAccentColor(m_accentColor);
    
    // Forward selection changes
    destSelector_->setOnSelectionChanged([this](int index) {
        if (onDestChanged_ && index >= 0 && index < static_cast<int>(destinations_.size())) {
            onDestChanged_(destinations_[index].first);
        }
    });

    levelKnob_ = std::make_shared<UIMixerKnob>(UIMixerKnobType::Send);
    levelKnob_->setAccentColor(m_accentColor);
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

void UIMixerSend::setAccentColor(const NUIColor& color)
{
    m_accentColor = color;
    if (destSelector_) {
        destSelector_->setAccentColor(color);
    }
    if (levelKnob_) {
        levelKnob_->setAccentColor(color);
    }
    repaint();
}

void UIMixerSend::onRender(NUIRenderer& renderer)
{
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    const float radius = 14.0f;

    renderer.drawShadow(b, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.10f));
    renderer.fillRoundedRect(b, radius, theme.getColor("backgroundSecondary").withAlpha(0.42f));
    renderer.strokeRoundedRect(b, radius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.36f));
    renderer.strokeRoundedRect({b.x + 1.0f, b.y + 1.0f, b.width - 2.0f, b.height - 2.0f},
                               radius - 1.0f,
                               1.0f,
                               NUIColor::white().withAlpha(0.02f));

    const NUIRect indexChip{b.x + 10.0f, b.y + 8.0f, 22.0f, 16.0f};
    renderer.fillRoundedRect(indexChip, 8.0f, m_accentColor.withAlpha(0.12f));
    renderer.strokeRoundedRect(indexChip, 8.0f, 1.0f, m_accentColor.withAlpha(0.20f));
    renderer.drawTextCentered(std::to_string(index_ + 1), indexChip, 9.0f, theme.getColor("textSecondary").withAlpha(0.94f));

    renderer.drawText("Route", {b.x + 40.0f, b.y + 10.0f}, 8.5f, theme.getColor("textSecondary").withAlpha(0.84f));

    const char* routeMode = m_postFader ? "Post" : "Pre";
    const float modeW = renderer.measureText(routeMode, 8.0f).width + 12.0f;
    const float muteW = m_muted ? renderer.measureText("Muted", 8.0f).width + 12.0f : 0.0f;
    float chipRight = b.right() - 8.0f;

    if (m_muted) {
        const NUIRect muteChip{chipRight - muteW, b.y + 8.0f, muteW, 15.0f};
        renderer.fillRoundedRect(muteChip, 7.5f, theme.getColor("warning").withAlpha(0.12f));
        renderer.strokeRoundedRect(muteChip, 7.5f, 1.0f, theme.getColor("warning").withAlpha(0.22f));
        renderer.drawTextCentered("Muted", muteChip, 8.0f, theme.getColor("textSecondary").withAlpha(0.92f));
        chipRight -= muteW + 4.0f;
    }

    const NUIRect modeChip{chipRight - modeW, b.y + 8.0f, modeW, 15.0f};
    renderer.fillRoundedRect(modeChip, 7.5f, theme.getColor("backgroundPrimary").withAlpha(0.34f));
    renderer.strokeRoundedRect(modeChip, 7.5f, 1.0f, theme.getColor("borderSubtle").withAlpha(0.22f));
    renderer.drawTextCentered(routeMode, modeChip, 8.0f, theme.getColor("textSecondary").withAlpha(0.90f));

    char levelBuf[32];
    const float level = std::max(0.0001f, levelKnob_ ? levelKnob_->getValue() : 1.0f);
    const float db = 20.0f * std::log10(level);
    if (db <= -59.9f) {
        std::snprintf(levelBuf, sizeof(levelBuf), "%s", "-inf dB");
    } else {
        std::snprintf(levelBuf, sizeof(levelBuf), "%.1f dB", db);
    }
    renderer.drawText(levelBuf, {b.right() - 74.0f, b.y + 11.0f}, 8.5f, theme.getColor("textSecondary").withAlpha(0.88f));

    const float knobSize = 22.0f;
    const float deleteBtnSize = 20.0f;
    const NUIRect knobRect{b.right() - 64.0f, b.y + 21.0f, knobSize, knobSize};
    const NUIRect deleteRect{b.right() - 28.0f, b.y + 22.0f, deleteBtnSize, deleteBtnSize};
    const float selectorRight = knobRect.x - 8.0f;
    const NUIRect comboRect{b.x + 40.0f, b.y + 22.0f, std::max(56.0f, selectorRight - (b.x + 40.0f)), 22.0f};

    renderer.drawText("Destination", {comboRect.x, b.y + 33.5f}, 7.5f, theme.getColor("textSecondary").withAlpha(0.66f));
    renderer.drawText("Level", {knobRect.x - 1.0f, b.y + 44.0f}, 7.5f, theme.getColor("textSecondary").withAlpha(0.66f));

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

} // namespace AestraUI
