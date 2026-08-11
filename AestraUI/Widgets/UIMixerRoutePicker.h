// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AestraUI {

class NUITextInput;

/**
 * Search-first mixer destination picker.
 *
 * Channel numbers are presentation only; selection callbacks always return the
 * stable mixer channel ID.
 */
class UIMixerRoutePicker : public NUIComponent {
public:
    struct Route {
        uint32_t id{0};
        int channelNumber{0};
        std::string name;
        uint32_t colorArgb{0};
    };

    UIMixerRoutePicker();

    void setRoutes(std::vector<Route> routes, uint32_t selectedRouteId);
    void setSelectedRoute(uint32_t routeId);
    uint32_t getSelectedRoute() const { return m_selectedRouteId; }
    void setTriggerBounds(const NUIRect& bounds);
    void setOnRouteSelected(std::function<void(uint32_t)> callback) { m_onRouteSelected = std::move(callback); }

    void setSearchQuery(const std::string& query);
    std::vector<uint32_t> getFilteredRouteIds() const;
    bool routeFirstMatch();

    bool isOpen() const { return m_open; }
    void close();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

private:
    static constexpr float kPopupWidth = 340.0f;
    static constexpr float kSearchHeight = 42.0f;
    static constexpr float kRowHeight = 36.0f;
    static constexpr float kFooterHeight = 30.0f;
    static constexpr int kVisibleRows = 6;

    std::vector<Route> m_routes;
    std::vector<size_t> m_filteredIndices;
    uint32_t m_selectedRouteId{0};
    std::string m_searchQuery;
    NUIRect m_triggerBounds;
    NUIRect m_popupBounds;
    int m_firstVisible{0};
    int m_hoveredVisibleRow{-1};
    bool m_open{false};

    std::shared_ptr<NUITextInput> m_searchInput;
    std::function<void(uint32_t)> m_onRouteSelected;

    void open();
    void rebuildFilter();
    void updatePopupBounds();
    void selectRoute(uint32_t routeId);
    const Route* selectedRoute() const;
    int hitTestVisibleRow(const NUIPoint& point) const;
};

} // namespace AestraUI
