// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerRoutePicker.h"

#include "NUIRenderer.h"
#include "NUITextInput.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace AestraUI {

namespace {

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string searchableQuery(std::string query) {
    query = lowercase(trim(std::move(query)));
    if (query.rfind("channel ", 0) == 0)
        query.erase(0, 8);
    else if (query.rfind("insert ", 0) == 0)
        query.erase(0, 7); // Legacy producer vocabulary remains accepted as input.
    if (!query.empty() && query.front() == '#')
        query.erase(query.begin());
    return trim(std::move(query));
}

bool contains(const std::string& haystack, const std::string& needle) {
    return needle.empty() || lowercase(haystack).find(needle) != std::string::npos;
}

bool isNumber(const std::string& text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char character) { return std::isdigit(character) != 0; });
}

NUIColor colorFromArgb(uint32_t argb) {
    return NUIColor(static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                    static_cast<float>(argb & 0xFF) / 255.0f, static_cast<float>((argb >> 24) & 0xFF) / 255.0f);
}

std::string routeNumberLabel(const UIMixerRoutePicker::Route& route) {
    if (route.channelNumber <= 0)
        return "M";
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(2) << route.channelNumber;
    return stream.str();
}

} // namespace

UIMixerRoutePicker::UIMixerRoutePicker() {
    setLayer(NUILayer::Dropdown);
    m_searchInput = std::make_shared<NUITextInput>();
    m_searchInput->setPlaceholderText("Type channel # or name…");
    m_searchInput->setShowPlaceholderWhenFocused(true);
    m_searchInput->setMaxLength(128);
    m_searchInput->setBorderRadius(5.0f);
    m_searchInput->setPadding(9.0f);
    m_searchInput->setVisible(false);
    m_searchInput->setOnTextChange([this](const std::string& query) {
        m_searchQuery = query;
        rebuildFilter();
        repaint();
    });
    m_searchInput->setOnReturnKey([this]() { routeFirstMatch(); });
    m_searchInput->setOnEscapeKey([this]() { close(); });
    addChild(m_searchInput);
}

void UIMixerRoutePicker::setRoutes(std::vector<Route> routes, uint32_t selectedRouteId) {
    m_routes = std::move(routes);
    m_selectedRouteId = selectedRouteId;
    if (!selectedRoute())
        m_selectedRouteId = 0;
    rebuildFilter();
    repaint();
}

void UIMixerRoutePicker::setSelectedRoute(uint32_t routeId) {
    const auto found =
        std::find_if(m_routes.begin(), m_routes.end(), [routeId](const Route& route) { return route.id == routeId; });
    m_selectedRouteId = found == m_routes.end() ? 0 : routeId;
    repaint();
}

void UIMixerRoutePicker::setTriggerBounds(const NUIRect& bounds) {
    m_triggerBounds = bounds;
    setBounds(bounds);
    updatePopupBounds();
}

void UIMixerRoutePicker::setSearchQuery(const std::string& query) {
    m_searchQuery = query;
    if (m_searchInput && m_searchInput->getText() != query)
        m_searchInput->setText(query);
    rebuildFilter();
}

std::vector<uint32_t> UIMixerRoutePicker::getFilteredRouteIds() const {
    std::vector<uint32_t> result;
    result.reserve(m_filteredIndices.size());
    for (const size_t index : m_filteredIndices)
        result.push_back(m_routes[index].id);
    return result;
}

bool UIMixerRoutePicker::routeFirstMatch() {
    if (m_filteredIndices.empty())
        return false;

    const std::string query = searchableQuery(m_searchQuery);
    size_t matchIndex = m_filteredIndices.front();
    if (!query.empty()) {
        const auto exact = std::find_if(m_filteredIndices.begin(), m_filteredIndices.end(), [&](size_t index) {
            const auto& route = m_routes[index];
            return (isNumber(query) && std::to_string(route.channelNumber) == query) || lowercase(route.name) == query;
        });
        if (exact != m_filteredIndices.end())
            matchIndex = *exact;
    }
    selectRoute(m_routes[matchIndex].id);
    return true;
}

void UIMixerRoutePicker::open() {
    if (m_open)
        return;
    m_open = true;
    m_firstVisible = 0;
    m_hoveredVisibleRow = -1;
    m_searchQuery.clear();
    rebuildFilter();
    updatePopupBounds();
    m_searchInput->setVisible(true);
    m_searchInput->setText("");
    m_searchInput->setFocused(true);
    bringToFront();
    repaint();
}

void UIMixerRoutePicker::close() {
    if (!m_open)
        return;
    m_open = false;
    m_hoveredVisibleRow = -1;
    m_searchInput->setVisible(false);
    m_searchInput->setFocused(false);
    repaint();
}

void UIMixerRoutePicker::rebuildFilter() {
    m_filteredIndices.clear();
    const std::string query = searchableQuery(m_searchQuery);
    const bool numericQuery = isNumber(query);
    for (size_t index = 0; index < m_routes.size(); ++index) {
        const auto& route = m_routes[index];
        if (query.empty() || contains(route.name, query) ||
            (numericQuery && std::to_string(route.channelNumber).find(query) != std::string::npos) ||
            (route.channelNumber == 0 && std::string("master").find(query) != std::string::npos)) {
            m_filteredIndices.push_back(index);
        }
    }

    if (numericQuery) {
        std::stable_sort(m_filteredIndices.begin(), m_filteredIndices.end(), [&](size_t left, size_t right) {
            const bool leftExact = std::to_string(m_routes[left].channelNumber) == query;
            const bool rightExact = std::to_string(m_routes[right].channelNumber) == query;
            return leftExact && !rightExact;
        });
    }
    const int maximumFirst = std::max(0, static_cast<int>(m_filteredIndices.size()) - kVisibleRows);
    m_firstVisible = std::clamp(m_firstVisible, 0, maximumFirst);
    m_hoveredVisibleRow = -1;
}

void UIMixerRoutePicker::updatePopupBounds() {
    const float width = std::max(kPopupWidth, m_triggerBounds.width);
    m_popupBounds = {m_triggerBounds.right() - width, m_triggerBounds.bottom() + 6.0f, width,
                     kSearchHeight + kRowHeight * kVisibleRows + kFooterHeight};
    if (m_searchInput) {
        m_searchInput->setBounds(
            {m_popupBounds.x + 8.0f, m_popupBounds.y + 6.0f, m_popupBounds.width - 16.0f, kSearchHeight - 12.0f});
    }
}

void UIMixerRoutePicker::selectRoute(uint32_t routeId) {
    setSelectedRoute(routeId);
    close();
    if (m_onRouteSelected)
        m_onRouteSelected(m_selectedRouteId);
}

const UIMixerRoutePicker::Route* UIMixerRoutePicker::selectedRoute() const {
    const auto found = std::find_if(m_routes.begin(), m_routes.end(),
                                    [this](const Route& route) { return route.id == m_selectedRouteId; });
    return found == m_routes.end() ? nullptr : &*found;
}

int UIMixerRoutePicker::hitTestVisibleRow(const NUIPoint& point) const {
    const NUIRect listBounds{m_popupBounds.x, m_popupBounds.y + kSearchHeight, m_popupBounds.width,
                             kRowHeight * kVisibleRows};
    if (!listBounds.contains(point))
        return -1;
    const int visibleRow = static_cast<int>((point.y - listBounds.y) / kRowHeight);
    const int filteredIndex = m_firstVisible + visibleRow;
    return filteredIndex < static_cast<int>(m_filteredIndices.size()) ? visibleRow : -1;
}

void UIMixerRoutePicker::onRender(NUIRenderer& renderer) {
    if (!isVisible())
        return;

    auto& theme = NUIThemeManager::getInstance();
    const auto textPrimary = theme.getColor("textPrimary");
    const auto textSecondary = theme.getColor("textSecondary");
    const auto border = theme.getColor("borderSubtle");
    const auto triggerBackground = theme.getColor("surfaceRaised").withAlpha(0.94f);
    const auto accent = theme.getColor("secondary");

    renderer.fillRoundedRect(m_triggerBounds, 6.0f,
                             isHovered() ? triggerBackground.lightened(0.05f) : triggerBackground);
    renderer.strokeRoundedRect(m_triggerBounds, 6.0f, m_open ? 1.5f : 1.0f, m_open ? accent.withAlpha(0.9f) : border);

    const Route* selected = selectedRoute();
    const std::string number = selected ? routeNumberLabel(*selected) : "M";
    const std::string name = selected ? selected->name : "Master";
    const NUIRect badge{m_triggerBounds.x + 7.0f, m_triggerBounds.y + 5.0f, 29.0f, m_triggerBounds.height - 10.0f};
    renderer.fillRoundedRect(badge, 4.0f, accent.withAlpha(0.17f));
    renderer.drawTextCentered(number, badge, 10.0f, accent.lightened(0.20f));
    renderer.drawText(name, {m_triggerBounds.x + 44.0f, m_triggerBounds.y + 8.0f}, 12.0f, textPrimary);
    renderer.drawText("FIND", {m_triggerBounds.right() - 39.0f, m_triggerBounds.y + 9.0f}, 9.0f, textSecondary);

    if (!m_open) {
        setDirty(false);
        return;
    }

    const auto popupBackground = theme.getColor("backgroundPrimary");
    const auto rowHover = theme.getColor("buttonBgHover").withAlpha(0.86f);
    renderer.fillRoundedRect(m_popupBounds, 8.0f, popupBackground);
    renderer.strokeRoundedRect(m_popupBounds, 8.0f, 1.0f, border.withAlpha(0.85f));

    renderer.fillRect({m_popupBounds.x + 1.0f, m_popupBounds.y + kSearchHeight, m_popupBounds.width - 2.0f, 1.0f},
                      border.withAlpha(0.55f));
    renderChildren(renderer);

    const NUIRect listBounds{m_popupBounds.x + 1.0f, m_popupBounds.y + kSearchHeight, m_popupBounds.width - 2.0f,
                             kRowHeight * kVisibleRows};
    renderer.setClipRect(listBounds);
    const int visibleCount = std::min(kVisibleRows, static_cast<int>(m_filteredIndices.size()) - m_firstVisible);
    for (int visibleRow = 0; visibleRow < visibleCount; ++visibleRow) {
        const auto& route = m_routes[m_filteredIndices[static_cast<size_t>(m_firstVisible + visibleRow)]];
        const float rowY = listBounds.y + visibleRow * kRowHeight;
        const NUIRect rowBounds{listBounds.x, rowY, listBounds.width, kRowHeight};
        const bool isSelected = route.id == m_selectedRouteId;
        if (visibleRow == m_hoveredVisibleRow || isSelected)
            renderer.fillRect(rowBounds, isSelected ? accent.withAlpha(0.13f) : rowHover);

        const NUIRect numberBounds{rowBounds.x + 9.0f, rowBounds.y + 7.0f, 31.0f, rowBounds.height - 14.0f};
        renderer.fillRoundedRect(numberBounds, 4.0f, accent.withAlpha(isSelected ? 0.24f : 0.10f));
        renderer.drawTextCentered(routeNumberLabel(route), numberBounds, 10.0f,
                                  isSelected ? accent.lightened(0.22f) : textSecondary);

        if (route.colorArgb != 0) {
            renderer.fillRoundedRect({rowBounds.x + 49.0f, rowBounds.y + 14.0f, 8.0f, 8.0f}, 2.0f,
                                     colorFromArgb(route.colorArgb));
        }
        renderer.drawText(route.name, {rowBounds.x + 65.0f, rowBounds.y + 10.0f}, 12.0f, textPrimary);
        if (isSelected)
            renderer.drawText("ROUTED", {rowBounds.right() - 53.0f, rowBounds.y + 11.0f}, 9.0f, accent);
    }
    renderer.clearClipRect();

    if (m_filteredIndices.empty()) {
        renderer.drawTextCentered("No matching channels", listBounds, 12.0f, textSecondary);
    }

    const float footerY = m_popupBounds.bottom() - kFooterHeight;
    renderer.fillRect({m_popupBounds.x + 1.0f, footerY, m_popupBounds.width - 2.0f, 1.0f}, border.withAlpha(0.55f));
    const std::string footer = m_filteredIndices.size() > kVisibleRows
                                   ? std::to_string(m_filteredIndices.size()) + " matches  •  wheel to browse"
                                   : "Type a number or name  •  Enter routes";
    renderer.drawText(footer, {m_popupBounds.x + 10.0f, footerY + 10.0f}, 10.0f, textSecondary);
    setDirty(false);
}

bool UIMixerRoutePicker::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled())
        return false;

    if (!m_open) {
        if (event.pressed && event.button == NUIMouseButton::Left && m_triggerBounds.contains(event.position)) {
            open();
            return true;
        }
        setHovered(m_triggerBounds.contains(event.position));
        return false;
    }

    if (m_searchInput && m_searchInput->getBounds().contains(event.position) && m_searchInput->onMouseEvent(event))
        return true;

    if (event.wheelDelta != 0.0f && m_popupBounds.contains(event.position)) {
        const int maximumFirst = std::max(0, static_cast<int>(m_filteredIndices.size()) - kVisibleRows);
        const int direction = event.wheelDelta > 0.0f ? -1 : 1;
        m_firstVisible = std::clamp(m_firstVisible + direction, 0, maximumFirst);
        m_hoveredVisibleRow = hitTestVisibleRow(event.position);
        repaint();
        return true;
    }

    if (m_popupBounds.contains(event.position)) {
        const int hovered = hitTestVisibleRow(event.position);
        if (hovered != m_hoveredVisibleRow) {
            m_hoveredVisibleRow = hovered;
            repaint();
        }
        if (event.pressed && event.button == NUIMouseButton::Left && hovered >= 0) {
            const int filteredIndex = m_firstVisible + hovered;
            selectRoute(m_routes[m_filteredIndices[static_cast<size_t>(filteredIndex)]].id);
        }
        return true;
    }

    if (m_triggerBounds.contains(event.position)) {
        if (event.pressed && event.button == NUIMouseButton::Left)
            close();
        return true;
    }

    if (event.pressed) {
        close();
        return true;
    }
    return false;
}

bool UIMixerRoutePicker::onKeyEvent(const NUIKeyEvent& event) {
    if (!m_open)
        return false;
    if (event.pressed && event.keyCode == NUIKeyCode::Escape) {
        close();
        return true;
    }
    if (event.pressed && event.keyCode == NUIKeyCode::Enter)
        return routeFirstMatch();
    return false;
}

} // namespace AestraUI
