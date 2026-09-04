// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Widgets/NUICoreWidgets.h"
#include "../../AestraAudio/include/Models/ClipSource.h"

#include <functional>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief Modal report for audio assets that went missing or moved (T-7, C-004).
 *
 * Shown after a project load when the serializer reported missing assets. The
 * project still loads — affected sources stay as retryable placeholders and
 * their clips play silent — and this dialog makes that loud instead of a log
 * line, with a per-file relink path: the app hosts the native file picker and
 * the engine rebind (relinkSource + attachBuffer); the dialog only tracks
 * which entries are still missing.
 */
class MissingAssetsDialog : public AestraUI::NUIComponent {
public:
    /// One missing project asset. sourceId is the loaded (unloaded-buffer)
    /// source to rebind; invalid means no source matched the stored path.
    struct MissingEntry {
        std::string storedPath;
        Aestra::Audio::ClipSourceID sourceId;
    };

    using RelinkRequestedCallback = std::function<void(const MissingEntry&)>;
    using DismissedCallback = std::function<void(std::size_t stillMissing)>;

    MissingAssetsDialog() = default;
    ~MissingAssetsDialog() override = default;

    // NUIComponent interface
    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;

    void show(std::vector<MissingEntry> entries, DismissedCallback onDismissed);
    void hide();
    bool isDialogVisible() const { return m_isVisible; }

    /// Fired when the user asks to relink an entry; the app opens the native
    /// picker and performs the engine rebind, then reports the outcome back.
    void setRelinkRequestedCallback(RelinkRequestedCallback callback) { m_relinkRequested = std::move(callback); }

    /// Drop a row after a successful relink; closes the dialog when the last
    /// one goes. A failed relink stays listed (no-op here beyond a repaint).
    void markRelinked(const std::string& storedPath);
    void markRelinkFailed(const std::string& storedPath);

    /// Entry the user most recently asked to relink (used to tag "failed").
    std::string lastRequestedPath() const { return m_lastRequested; }

private:
    struct RowRects {
        AestraUI::NUIRect pathRect;
        AestraUI::NUIRect buttonRect;
    };

    void calculateLayout();
    void handleDismiss();

    std::vector<MissingEntry> m_entries;
    std::vector<RowRects> m_rowRects;
    AestraUI::NUIRect m_dialogRect;
    AestraUI::NUIRect m_dismissButtonRect;
    std::string m_lastRequested;
    int m_hoveredRow{-1};
    bool m_dismissHovered{false};
    bool m_dismissPressed{false};
    int m_pressedRow{-1};
    bool m_isVisible{false};

    RelinkRequestedCallback m_relinkRequested;
    DismissedCallback m_onDismissed;
};

} // namespace Aestra
