// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIMixerWidgets.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "../Platform/NUIPlatformBridge.h"
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
    : text_("Insert"), color_(NUIColor::fromHex(0xff6633ff))
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
    const auto& props = theme.getCurrentTheme();
    NUIColor base = theme.getColor("muted");
    auto colors = resolveControlColors(props, {true, isHovered(), false, isOn(), isFocused()});
    NUIColor bg = colors.background;
    NUIColor border = colors.border;
    NUIColor text = colors.text;

    if (isOn()) {
        bg = base.withAlpha(0.16f);
        border = base.withAlpha(0.52f);
        text = theme.getColor("textPrimary");
    }

    renderer.fillRoundedRect(b, props.radiusM, bg);
    renderer.strokeRoundedRect(b, props.radiusM, colors.borderWidth, border);
    renderer.drawTextCentered("M", b, props.fontSizeM, text);
}

SoloButton::SoloButton()
{
    setOn(false);
}

void SoloButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    const auto& props = theme.getCurrentTheme();
    NUIColor base = theme.getColor("soloed");
    auto colors = resolveControlColors(props, {true, isHovered(), false, isOn(), isFocused()});
    NUIColor bg = colors.background;
    NUIColor border = colors.border;
    NUIColor text = colors.text;

    if (isOn()) {
        bg = base.withAlpha(0.16f);
        border = base.withAlpha(0.52f);
        text = theme.getColor("textPrimary");
    }

    renderer.fillRoundedRect(b, props.radiusM, bg);
    renderer.strokeRoundedRect(b, props.radiusM, colors.borderWidth, border);
    renderer.drawTextCentered("S", b, props.fontSizeM, text);
}

ArmButton::ArmButton()
{
    setOn(false);
}

void ArmButton::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    
    const auto& props = theme.getCurrentTheme();
    NUIColor base = theme.getColor("armed");
    auto colors = resolveControlColors(props, {true, isHovered(), false, isOn(), isFocused()});
    NUIColor bg = colors.background;
    NUIColor border = colors.border;
    NUIColor text = colors.text;

    if (isOn()) {
        bg = base.withAlpha(0.16f);
        border = base.withAlpha(0.52f);
        text = theme.getColor("textPrimary");
    }

    renderer.fillRoundedRect(b, props.radiusM, bg);
    renderer.strokeRoundedRect(b, props.radiusM, colors.borderWidth, border);
    
    // Circle or icon for Record? Keeping "R" for consistency but could be circle
    // Let's use a small filled circle for R to look like a rec light
    if (isOn()) {
        float cx = b.x + b.width * 0.5f;
        float cy = b.y + b.height * 0.5f;
        renderer.fillCircle({cx, cy}, 3.5f, base.withAlpha(0.92f));
    } else {
        renderer.drawTextCentered("R", b, props.fontSizeM, text);
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

    m_modeControl = std::make_shared<NUISegmentedControl>(std::vector<std::string>{"Pre", "Post"});
    m_modeControl->setCornerRadius(8.0f);
    m_modeControl->setAccentColor(m_accentColor);
    m_modeControl->setSelectedIndex(1, false);
    m_modeControl->setOnSelectionChanged([this](size_t index) {
        const bool post = (index == 1);
        if (m_postFader != post) {
            m_postFader = post;
            if (m_onPostFaderChanged) {
                m_onPostFaderChanged(post);
            }
            repaint();
        }
    });

    m_sendTypeControl = std::make_shared<NUISegmentedControl>(std::vector<std::string>{"Audio", "SC"});
    m_sendTypeControl->setCornerRadius(8.0f);
    m_sendTypeControl->setAccentColor(m_accentColor);
    m_sendTypeControl->setSelectedIndex(0, false);
    m_sendTypeControl->setOnSelectionChanged([this](size_t index) {
        const bool scOnly = (index == 1);
        if (m_sidechainOnly != scOnly) {
            m_sidechainOnly = scOnly;
            if (m_onSidechainModeChanged) {
                m_onSidechainModeChanged(scOnly);
            }
            repaint();
        }
    });

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
    addChild(m_modeControl);
    addChild(m_sendTypeControl);
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
    if (m_modeControl) {
        m_modeControl->setAccentColor(color);
    }
    if (m_sendTypeControl) {
        m_sendTypeControl->setAccentColor(color);
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

    const char* sendKind = m_muted ? "Muted" : (m_sidechainOnly ? "SC" : "Audio");
    const float kindW = renderer.measureText(sendKind, 8.0f).width + 12.0f;
    const NUIRect kindChip{b.right() - kindW - 10.0f, b.y + 8.0f, kindW, 15.0f};
    renderer.fillRoundedRect(kindChip,
                             7.5f,
                             m_muted ? theme.getColor("warning").withAlpha(0.12f)
                                     : theme.getColor("backgroundPrimary").withAlpha(0.26f));
    renderer.strokeRoundedRect(kindChip,
                               7.5f,
                               1.0f,
                               m_muted ? theme.getColor("warning").withAlpha(0.22f)
                                       : theme.getColor("borderSubtle").withAlpha(0.18f));
    renderer.drawTextCentered(sendKind, kindChip, 8.0f, theme.getColor("textSecondary").withAlpha(0.92f));

    char levelBuf[32];
    const float level = std::max(0.0001f, levelKnob_ ? levelKnob_->getValue() : 1.0f);
    const float db = 20.0f * std::log10(level);
    if (db <= -59.9f) {
        std::snprintf(levelBuf, sizeof(levelBuf), "%s", "-inf dB");
    } else {
        std::snprintf(levelBuf, sizeof(levelBuf), "%.1f dB", db);
    }

    const float knobSize = 26.0f;
    const float deleteBtnSize = 20.0f;
    const float modeW = 64.0f;
    const float typeW = 64.0f;
    const NUIRect knobRect{b.right() - 42.0f, b.y + 48.0f, knobSize, knobSize};
    const NUIRect comboRect{b.x + 12.0f, b.y + 52.0f, b.width - 66.0f, 22.0f};
    const NUIRect deleteRect{b.x + 12.0f, b.bottom() - 30.0f, deleteBtnSize, deleteBtnSize};
    const NUIRect modeGroupRect{deleteRect.right() + 10.0f, b.bottom() - 31.0f, modeW, 24.0f};
    const NUIRect typeGroupRect{modeGroupRect.right() + 8.0f, b.bottom() - 31.0f, typeW, 24.0f};
    const NUIRect modeRect{modeGroupRect.x, modeGroupRect.y + 8.0f, modeW, 16.0f};
    const NUIRect typeRect{typeGroupRect.x, typeGroupRect.y + 8.0f, typeW, 16.0f};

    renderer.drawText("SEND", {indexChip.right() + 8.0f, b.y + 11.0f}, 8.5f,
                      theme.getColor("textSecondary").withAlpha(0.74f));
    const float levelW = renderer.measureText(levelBuf, 8.0f).width;
    renderer.drawText(levelBuf,
                      {knobRect.x + knobRect.width * 0.5f - levelW * 0.5f, b.y + 27.0f},
                      8.0f, theme.getColor("textPrimary").withAlpha(0.86f));

    renderer.drawText("Destination", {comboRect.x, b.y + 38.0f}, 7.5f, theme.getColor("textSecondary").withAlpha(0.76f));
    if (m_modeEditable) {
        renderer.drawText("Tap", {modeGroupRect.x, modeGroupRect.y - 10.0f}, 7.25f,
                          theme.getColor("textSecondary").withAlpha(0.76f));
    }
    renderer.drawText("Send", {typeGroupRect.x, typeGroupRect.y - 10.0f}, 7.25f,
                      theme.getColor("textSecondary").withAlpha(0.76f));

    levelKnob_->setBounds(knobRect);
    destSelector_->setBounds(comboRect);
    m_modeControl->setVisible(m_modeEditable);
    m_modeControl->setBounds(modeRect);
    m_sendTypeControl->setBounds(typeRect);
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

void UIMixerSend::setPostFader(bool postFader)
{
    m_postFader = postFader;
    if (m_modeControl) {
        m_modeControl->setSelectedIndex(postFader ? 1u : 0u, false);
    }
    repaint();
}

void UIMixerSend::setSidechainOnly(bool sidechainOnly)
{
    m_sidechainOnly = sidechainOnly;
    if (m_sendTypeControl) {
        m_sendTypeControl->setSelectedIndex(sidechainOnly ? 1u : 0u, false);
    }
    repaint();
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

void UIMixerSend::setOnPostFaderChanged(std::function<void(bool)> cb)
{
    m_onPostFaderChanged = std::move(cb);
}

void UIMixerSend::setOnSidechainModeChanged(std::function<void(bool)> cb)
{
    m_onSidechainModeChanged = std::move(cb);
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

void ChannelStrip::setPlatformBridge(NUIPlatformBridge* bridge)
{
    m_platformBridge = bridge;
    if (panKnob_) {
        panKnob_->setPlatformBridge(bridge);
    }
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
