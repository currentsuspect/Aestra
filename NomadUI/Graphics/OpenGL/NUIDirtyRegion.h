#ifndef NUIDIRTYREGION_H
#define NUIDIRTYREGION_H

#include "../../Core/NUITypes.h"
#include <vector>
#include <algorithm>

namespace NomadUI {

    // Manages dirty regions that need repainting
    class NUIDirtyRegionManager {
    public:
        NUIDirtyRegionManager();
        ~NUIDirtyRegionManager();

        // Mark a region as dirty
        void markDirty(const NUIRect& rect);

        // Mark entire screen as dirty
        void markAllDirty(const NUISize& screenSize);

        // Check if a region is dirty
        bool isDirty(const NUIRect& rect) const;

        // Get all dirty regions
        const std::vector<NUIRect>& getDirtyRegions() const { return m_dirtyRegions; }

        // Clear all dirty regions
        void clear();

        // Optimize dirty regions (merge overlapping/adjacent regions)
        void optimize();

        // Enable/disable dirty tracking
        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        // Get stats
        size_t getDirtyRegionCount() const { return m_dirtyRegions.size(); }
        float getDirtyCoverage(const NUISize& screenSize) const;

    private:
        bool rectsIntersect(const NUIRect& a, const NUIRect& b) const;
        bool rectsAdjacent(const NUIRect& a, const NUIRect& b, float threshold = 5.0f) const;
        NUIRect mergeRects(const NUIRect& a, const NUIRect& b) const;

        std::vector<NUIRect> m_dirtyRegions;
        bool m_enabled;
        bool m_allDirty;
        NUISize m_screenSize;
    };

    // Dirty state tracking for widgets
    enum class DirtyFlags : uint32_t {
        None = 0,
        Position = 1 << 0,      // Widget moved
        Size = 1 << 1,          // Widget resized
        Content = 1 << 2,       // Widget content changed (text, color, etc.)
        Children = 1 << 3,      // Children changed
        Visibility = 1 << 4,    // Visibility changed
        State = 1 << 5,         // Widget state changed (hover, pressed, etc.)
        All = 0xFFFFFFFF
    };

    inline DirtyFlags operator|(DirtyFlags a, DirtyFlags b) {
        return static_cast<DirtyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline DirtyFlags operator&(DirtyFlags a, DirtyFlags b) {
        return static_cast<DirtyFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline DirtyFlags& operator|=(DirtyFlags& a, DirtyFlags b) {
        a = a | b;
        return a;
    }

    // Widget dirty state tracker
    class NUIWidgetDirtyState {
    public:
        NUIWidgetDirtyState();

        // Mark widget as dirty
        void markDirty(DirtyFlags flags = DirtyFlags::All);

        // Clear dirty state
        void clearDirty();

        // Check if dirty
        bool isDirty() const { return m_dirtyFlags != DirtyFlags::None; }
        bool hasDirtyFlag(DirtyFlags flag) const {
            return (m_dirtyFlags & flag) != DirtyFlags::None;
        }

        // Get dirty flags
        DirtyFlags getDirtyFlags() const { return m_dirtyFlags; }

        // Set last rendered bounds (for optimization)
        void setLastRenderedBounds(const NUIRect& bounds) { m_lastRenderedBounds = bounds; }
        const NUIRect& getLastRenderedBounds() const { return m_lastRenderedBounds; }

    private:
        DirtyFlags m_dirtyFlags;
        NUIRect m_lastRenderedBounds;
    };

} // namespace NomadUI

#endif // NUIDIRTYREGION_H
