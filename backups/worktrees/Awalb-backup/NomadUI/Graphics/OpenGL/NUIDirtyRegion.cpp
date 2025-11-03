// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDirtyRegion.h"
#include <cmath>

namespace NomadUI {

    // ========================================================================================
    // NUIDirtyRegionManager
    // ========================================================================================

    NUIDirtyRegionManager::NUIDirtyRegionManager()
        : m_enabled(true)
        , m_allDirty(true)
        , m_screenSize(0, 0)
    {
    }

    NUIDirtyRegionManager::~NUIDirtyRegionManager() {
    }

    void NUIDirtyRegionManager::markDirty(const NUIRect& rect) {
        if (!m_enabled) return;

        // If already all dirty, no need to add more regions
        if (m_allDirty) return;

        // Add the dirty region
        m_dirtyRegions.push_back(rect);

        // Optimize if we have too many regions
        if (m_dirtyRegions.size() > 20) {
            optimize();
        }
    }

    void NUIDirtyRegionManager::markAllDirty(const NUISize& screenSize) {
        m_allDirty = true;
        m_screenSize = screenSize;
        m_dirtyRegions.clear();
        m_dirtyRegions.push_back(NUIRect(0, 0, screenSize.width, screenSize.height));
    }

    bool NUIDirtyRegionManager::isDirty(const NUIRect& rect) const {
        if (!m_enabled) return true; // If disabled, everything is dirty
        if (m_allDirty) return true;
        if (m_dirtyRegions.empty()) return false;

        // Check if rect intersects with any dirty region
        for (const auto& dirtyRect : m_dirtyRegions) {
            if (rectsIntersect(rect, dirtyRect)) {
                return true;
            }
        }

        return false;
    }

    void NUIDirtyRegionManager::clear() {
        m_dirtyRegions.clear();
        m_allDirty = false;
    }

    void NUIDirtyRegionManager::optimize() {
        if (m_dirtyRegions.size() <= 1) return;

        bool merged = true;
        while (merged && m_dirtyRegions.size() > 1) {
            merged = false;

            for (size_t i = 0; i < m_dirtyRegions.size() && !merged; ++i) {
                for (size_t j = i + 1; j < m_dirtyRegions.size() && !merged; ++j) {
                    // Check if regions intersect or are adjacent
                    if (rectsIntersect(m_dirtyRegions[i], m_dirtyRegions[j]) ||
                        rectsAdjacent(m_dirtyRegions[i], m_dirtyRegions[j])) {
                        
                        // Merge the regions
                        NUIRect mergedRect = mergeRects(m_dirtyRegions[i], m_dirtyRegions[j]);
                        
                        // Remove the two regions and add the merged one
                        m_dirtyRegions[i] = mergedRect;
                        m_dirtyRegions.erase(m_dirtyRegions.begin() + j);
                        merged = true;
                    }
                }
            }
        }

        // If the dirty area covers most of the screen, just mark all dirty
        if (m_screenSize.width > 0 && m_screenSize.height > 0) {
            float totalDirtyArea = 0;
            for (const auto& rect : m_dirtyRegions) {
                totalDirtyArea += rect.width * rect.height;
            }
            float screenArea = m_screenSize.width * m_screenSize.height;
            if (totalDirtyArea > screenArea * 0.75f) {
                markAllDirty(m_screenSize);
            }
        }
    }

    float NUIDirtyRegionManager::getDirtyCoverage(const NUISize& screenSize) const {
        if (m_allDirty || screenSize.width <= 0 || screenSize.height <= 0) return 1.0f;

        float totalDirtyArea = 0;
        for (const auto& rect : m_dirtyRegions) {
            totalDirtyArea += rect.width * rect.height;
        }

        float screenArea = screenSize.width * screenSize.height;
        return (screenArea > 0) ? (totalDirtyArea / screenArea) : 0.0f;
    }

    bool NUIDirtyRegionManager::rectsIntersect(const NUIRect& a, const NUIRect& b) const {
        return !(a.x + a.width < b.x ||
                 b.x + b.width < a.x ||
                 a.y + a.height < b.y ||
                 b.y + b.height < a.y);
    }

    bool NUIDirtyRegionManager::rectsAdjacent(const NUIRect& a, const NUIRect& b, float threshold) const {
        // Check if rectangles are close enough to be considered adjacent
        float dx = std::max(0.0f, std::max(a.x - (b.x + b.width), b.x - (a.x + a.width)));
        float dy = std::max(0.0f, std::max(a.y - (b.y + b.height), b.y - (a.y + a.height)));
        return (dx <= threshold && dy <= threshold);
    }

    NUIRect NUIDirtyRegionManager::mergeRects(const NUIRect& a, const NUIRect& b) const {
        float left = std::min(a.x, b.x);
        float top = std::min(a.y, b.y);
        float right = std::max(a.x + a.width, b.x + b.width);
        float bottom = std::max(a.y + a.height, b.y + b.height);

        return NUIRect(left, top, right - left, bottom - top);
    }

    // ========================================================================================
    // NUIWidgetDirtyState
    // ========================================================================================

    NUIWidgetDirtyState::NUIWidgetDirtyState()
        : m_dirtyFlags(DirtyFlags::All) // Start dirty
        , m_lastRenderedBounds(0, 0, 0, 0)
    {
    }

    void NUIWidgetDirtyState::markDirty(DirtyFlags flags) {
        m_dirtyFlags |= flags;
    }

    void NUIWidgetDirtyState::clearDirty() {
        m_dirtyFlags = DirtyFlags::None;
    }

} // namespace NomadUI
