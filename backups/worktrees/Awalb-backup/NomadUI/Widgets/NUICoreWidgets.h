// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUIButton.h"
#include "../Core/NUITextInput.h"
#include "../Core/NUIIcon.h"
#include "../Core/NUITypes.h"
#include <functional>
#include <string>
#include <vector>

namespace NomadUI {

class NUIToggle : public NUIComponent {
public:
    enum class State {
        Off,
        On,
        Disabled
    };

    NUIToggle();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

    void setState(State state);
    State getState() const { return state_; }

    void setAnimated(bool animated);
    bool isAnimated() const { return animated_; }

    void setOnToggle(std::function<void(bool)> callback);

    bool isOn() const { return state_ == State::On; }
    void setOn(bool enabled);

private:
    void updateVisualState();

    State state_;
    bool animated_;
    bool hovered_;
    std::function<void(bool)> onToggle_;
};

class NUITextField : public NUITextInput {
public:
    NUITextField();

    void setPlaceholder(const std::string& text);
    const std::string& getPlaceholder() const { return placeholder_; }

    void onRender(NUIRenderer& renderer) override;

private:
    std::string placeholder_;
};

class NUIMeter : public NUIComponent {
public:
    struct ChannelLevel {
        float peak = 0.0f;
        float rms = 0.0f;
    };

    NUIMeter();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;

    void setChannelCount(size_t count);
    size_t getChannelCount() const { return channels_.size(); }

    void setLevels(size_t channel, float peak, float rms);
    ChannelLevel getLevels(size_t channel) const;

    void setDecayRate(float rate);
    float getDecayRate() const { return decayRate_; }

    void setHoldEnabled(bool enabled);
    bool isHoldEnabled() const { return holdEnabled_; }

private:
    std::vector<ChannelLevel> channels_;
    float decayRate_;
    bool holdEnabled_;
};

class NUIScrollView : public NUIComponent {
public:
    enum class Direction {
        Horizontal,
        Vertical,
        Both
    };

    NUIScrollView();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setContentSize(const NUISize& size);
    NUISize getContentSize() const { return contentSize_; }

    void setScrollOffset(const NUIPoint& offset);
    NUIPoint getScrollOffset() const { return scrollOffset_; }

    void setDirection(Direction direction);
    Direction getDirection() const { return direction_; }

private:
    NUIPoint clampOffset(const NUIPoint& offset) const;

    NUISize contentSize_;
    NUIPoint scrollOffset_;
    Direction direction_;
};

class NUIPanel : public NUIComponent {
public:
    enum class Variant {
        Plain,
        Elevated,
        Outlined
    };

    NUIPanel();

    void onRender(NUIRenderer& renderer) override;

    void setBackgroundColor(const NUIColor& color);
    NUIColor getBackgroundColor() const { return backgroundColor_; }

    void setBorderColor(const NUIColor& color);
    NUIColor getBorderColor() const { return borderColor_; }

    void setVariant(Variant variant);
    Variant getVariant() const { return variant_; }

private:
    NUIColor backgroundColor_;
    NUIColor borderColor_;
    Variant variant_;
};

struct NUIPopupMenuItem {
    std::string id;
    std::string label;
    bool enabled = true;
};

class NUIPopupMenu : public NUIComponent {
public:
    NUIPopupMenu();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setItems(std::vector<NUIPopupMenuItem> items);
    const std::vector<NUIPopupMenuItem>& getItems() const { return items_; }

    void setOnSelect(std::function<void(const NUIPopupMenuItem&)> callback);

private:
    std::vector<NUIPopupMenuItem> items_;
    std::function<void(const NUIPopupMenuItem&)> onSelect_;
};

class NUITabBar : public NUIComponent {
public:
    struct Tab {
        std::string id;
        std::string label;
        bool closeable = false;
    };

    NUITabBar();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void addTab(const Tab& tab);
    void removeTab(const std::string& id);
    void clearTabs();

    void setActiveTab(const std::string& id);
    std::string getActiveTab() const { return activeTabId_; }

    void setOnTabChanged(std::function<void(const std::string&)> callback);

    const std::vector<Tab>& getTabs() const { return tabs_; }

private:
    int hitTestTab(int x, int y) const;

    std::vector<Tab> tabs_;
    std::string activeTabId_;
    std::function<void(const std::string&)> onTabChanged_;
};

} // namespace NomadUI

