// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RecoveryDialog.h"
#include "../AestraUI/Widgets/AestraPanelWindow.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../Panels/VersionsTimeline.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Aestra {

RecoveryDialog::RecoveryDialog()
    : m_response(RecoveryResponse::None)
    , m_isVisible(false)
    , m_recoverHovered(false)
    , m_discardHovered(false)
{
}

bool RecoveryDialog::detectAutosave(const std::string& autosavePath, std::string& outTimestamp) {
    if (!std::filesystem::exists(autosavePath)) {
        return false;
    }
    
    // Get file modification time
    try {
        auto ftime = std::filesystem::last_write_time(autosavePath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time = std::chrono::system_clock::to_time_t(sctp);
        
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        outTimestamp = oss.str();
        
        // Check if file is not empty
        auto fileSize = std::filesystem::file_size(autosavePath);
        if (fileSize == 0) {
            return false;
        }
        
        Log::info("[Recovery] Detected autosave: " + autosavePath + " (" + outTimestamp + ")");
        return true;
    } catch (const std::exception& e) {
        Log::warning("[Recovery] Error checking autosave: " + std::string(e.what()));
        return false;
    }
}

bool RecoveryDialog::detectAutosave(const std::string& autosavePath,
                                    const std::string& recoveryMarkerPath,
                                    const std::string& expectedSessionToken,
                                    std::string& outTimestamp) {
    if (expectedSessionToken.empty()) {
        return false;
    }

    std::ifstream marker(recoveryMarkerPath, std::ios::binary);
    if (!marker.good()) {
        Log::warning("[Recovery] Recovery marker missing; falling back to legacy autosave detection");
        return detectAutosave(autosavePath, outTimestamp);
    }

    std::string markerToken;
    std::getline(marker, markerToken);
    if (marker.fail() && !marker.eof()) {
        Log::warning("[Recovery] Recovery marker unreadable; falling back to legacy autosave detection");
        return detectAutosave(autosavePath, outTimestamp);
    }

    if (markerToken != expectedSessionToken) {
        Log::warning("[Recovery] Recovery marker does not match current session; falling back to legacy autosave detection");
        return detectAutosave(autosavePath, outTimestamp);
    }

    return detectAutosave(autosavePath, outTimestamp);
}

void RecoveryDialog::show(const std::string& autosavePath, ResponseCallback callback, const RecoveryInfo::Info& info) {
    m_autosavePath = autosavePath;
    m_info = info;
    m_altHovered = false;
    m_callback = callback;
    m_response = RecoveryResponse::None;
    m_pressedResponse = RecoveryResponse::None;
    m_isVisible = true;
    
    // Get timestamp
    detectAutosave(autosavePath, m_autosaveTimestamp);
    
    setVisible(true);
    Log::info("[Recovery] Showing recovery dialog");
}

void RecoveryDialog::hide() {
    m_isVisible = false;
    setVisible(false);
}

void RecoveryDialog::calculateLayout() {
    auto bounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();

    // The card: banner, a small timeline, two lines of explanation, three buttons.
    const float dialogWidth = std::min(900.0f, bounds.width - 40.0f);
    const float dialogHeight = 392.0f + AestraUI::AestraPanelWindow::TITLE_BAR_H;
    m_dialogRect = AestraUI::NUIRect((bounds.width - dialogWidth) * 0.5f, (bounds.height - dialogHeight) * 0.5f, dialogWidth, dialogHeight);

    const float buttonHeight = 38.0f;
    const float buttonY = m_dialogRect.bottom() - buttonHeight - 28.0f;
    float x = m_dialogRect.x + 32.0f;
    const auto place = [&](float w) { AestraUI::NUIRect r(x, buttonY, w, buttonHeight); x += w + theme.spacingM; return r; };
    m_recoverButtonRect = place(204.0f);
    m_altButtonRect = place(176.0f);
    m_discardButtonRect = place(188.0f);
}

void RecoveryDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_isVisible) return;

    calculateLayout();

    auto bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& theme = themeManager.getCurrentTheme();
    const AestraUI::NUIColor amber = themeManager.getColor("warning");
    const AestraUI::NUIColor text = theme.textPrimary, muted = theme.textSecondary;

    renderer.fillRect(AestraUI::NUIRect(0, 0, bounds.width, bounds.height), theme.overlay);

    AestraUI::DialogChrome chrome;
    chrome.panel = m_dialogRect;
    chrome.title = "Recover Unsaved Work";
    chrome.showClose = false; // None of the answers is a safe dismiss.
    const AestraUI::NUIRect c = AestraUI::drawDialogChrome(renderer, chrome);

    // Banner: said plainly, and kindly.
    renderer.fillRect({c.x, c.y, c.width, 60.0f}, amber.withAlpha(0.10f));
    renderer.fillCircle({c.x + 32.0f, c.y + 30.0f}, 5.0f, amber);
    const std::string headline = "Aestra closed unexpectedly. Your work is still here.";
    renderer.drawText(headline, {c.x + 52.0f, c.y + 22.0f}, 15.0f, text);
    const float hw = renderer.measureText(headline, 15.0f).width;
    renderer.drawText(m_info.detail.empty() ? "An autosave was found from " + m_autosaveTimestamp : m_info.detail, {c.x + 52.0f + hw + 16.0f, c.y + 24.0f}, 12.0f, muted);
    renderer.fillRect({c.x, c.y + 60.0f, c.width, 1.0f}, theme.borderSubtle);

    // The timeline: the last saved version, then the autosaves after it, lit.
    const float laneY = c.y + 150.0f;
    const float left = c.x + 200.0f, right = c.right() - 120.0f;
    const AestraUI::NUIColor laneColor = AestraUI::NUIColor::fromHex(VersionLanes::hex(m_info.lane));
    const size_t dots = m_info.autosaveMs.size();
    const float savedX = m_info.hasVersion ? left + (right - left) * 0.25f : left;
    if (m_info.hasVersion) {
        renderer.fillCircle({c.x + 32.0f, laneY}, 3.5f, laneColor);
        renderer.drawText(m_info.branch, {c.x + 44.0f, laneY - 6.0f}, 11.0f, text);
        renderer.drawLine({left, laneY}, {savedX, laneY}, 2.0f, laneColor.withAlpha(0.45f));
        renderer.fillCircle({savedX, laneY}, 9.0f, laneColor);
        const float nw = renderer.measureText(m_info.lastSavedName, 12.0f).width;
        renderer.drawText(m_info.lastSavedName, {savedX - nw * 0.5f, laneY - 44.0f}, 12.0f, text);
    }
    const float lastX = dots ? savedX + (right - savedX) * (m_info.hasVersion ? 1.0f : 1.0f) : savedX;
    if (dots) {
        renderer.drawLine({savedX, laneY}, {lastX, laneY}, 2.0f, amber);
        for (size_t i = 0; i < dots; ++i) {
            const bool newest = i + 1 == dots;
            const float x = savedX + (lastX - savedX) * static_cast<float>(i + 1) / static_cast<float>(dots);
            const std::string clock = VersionsTimeline::clockLabel(m_info.autosaveMs[i]);
            if (newest) {
                renderer.fillCircle({x, laneY}, 17.0f, amber.withAlpha(0.22f));
                renderer.fillCircle({x, laneY}, 12.0f, amber);
                const float aw = renderer.measureText("Autosave", 12.0f).width;
                renderer.drawText("Autosave", {x - aw * 0.5f, laneY - 48.0f}, 12.0f, text);
                const std::string sub = clock + " \xc2\xb7 newest";
                const float sw = renderer.measureText(sub, 10.5f).width;
                renderer.drawText(sub, {x - sw * 0.5f, laneY - 30.0f}, 10.5f, amber);
            } else {
                renderer.fillCircle({x, laneY}, 8.0f, amber);
                const float cw = renderer.measureText(clock, 10.5f).width;
                renderer.drawText(clock, {x - cw * 0.5f, laneY + 22.0f}, 10.5f, amber);
            }
        }
    }
    if (m_info.hasVersion) {
        const std::string sub = "last saved \xc2\xb7 " + VersionsTimeline::clockLabel(m_info.lastSavedMs);
        const float sw = renderer.measureText(sub, 10.5f).width;
        renderer.drawText(sub, {savedX - sw * 0.5f, laneY - 28.0f}, 10.5f, muted);
    }

    const float textY = laneY + 70.0f;
    renderer.drawText("Autosaves always show as small dots on the timeline; here they are lit because they hold work you have not saved.",
                      {c.x + 32.0f, textY}, 11.5f, muted);
    renderer.drawText("Open the last saved version and the autosave is kept as a rescued session, so nothing is thrown away.",
                      {c.x + 32.0f, textY + 20.0f}, 11.5f, muted);

    const auto button = [&](const AestraUI::NUIRect& r, const std::string& label, bool primary, bool hovered, bool pressed) {
        AestraUI::NUIColor fill = primary ? amber : theme.buttonBgDefault;
        if (pressed) fill = primary ? amber.darkened(0.15f) : theme.pressed;
        else if (hovered) fill = primary ? amber.lightened(0.08f) : theme.hover;
        renderer.fillRect(r, fill);
        if (!primary) renderer.strokeRect(r, 1.0f, theme.borderStrong);
        const float tw = renderer.measureText(label, 13.0f).width;
        renderer.drawText(label, {r.x + (r.width - tw) * 0.5f, std::round(renderer.calculateTextY(r, 13.0f))}, 13.0f,
                          primary ? AestraUI::NUIColor(0.10f, 0.07f, 0.02f, 1.0f) : text);
    };
    button(m_recoverButtonRect, "Recover newest autosave", true, m_recoverHovered, m_pressedResponse == RecoveryResponse::Recover);
    button(m_altButtonRect, "Recover as an alt", false, m_altHovered, m_pressedResponse == RecoveryResponse::RecoverAsAlt);
    button(m_discardButtonRect, m_info.hasVersion ? "Open last saved version" : "Start a new project", false, m_discardHovered,
           m_pressedResponse == RecoveryResponse::OpenLastSaved);
}

bool RecoveryDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_isVisible) return false;

    // Input may arrive before the first rendered frame or immediately after a
    // resize. Hit-test against current bounds rather than stale/default rects.
    calculateLayout();
    
    float mx = event.position.x;
    float my = event.position.y;
    
    // Update hover states
    const bool recoverHovered = m_recoverButtonRect.contains(mx, my);
    const bool discardHovered = m_discardButtonRect.contains(mx, my);
    const bool altHovered = m_altButtonRect.contains(mx, my);
    if (recoverHovered != m_recoverHovered || discardHovered != m_discardHovered || altHovered != m_altHovered) {
        m_recoverHovered = recoverHovered;
        m_discardHovered = discardHovered;
        m_altHovered = altHovered;
        setDirty(true);
    }
    
    if (event.button == AestraUI::NUIMouseButton::Left) {
        if (event.pressed) {
            m_pressedResponse = m_recoverHovered ? RecoveryResponse::Recover
                              : m_altHovered ? RecoveryResponse::RecoverAsAlt
                              : m_discardHovered ? RecoveryResponse::OpenLastSaved
                                                 : RecoveryResponse::None;
            setDirty(true);
            return true;
        }
        if (event.released) {
            const RecoveryResponse armed = m_pressedResponse;
            m_pressedResponse = RecoveryResponse::None;
            setDirty(true);
            if ((armed == RecoveryResponse::Recover && m_recoverHovered) ||
                (armed == RecoveryResponse::RecoverAsAlt && m_altHovered) ||
                (armed == RecoveryResponse::OpenLastSaved && m_discardHovered)) {
                handleResponse(armed);
            }
            return true;
        }
    }
    
    // Block all mouse events when visible
    return true;
}

bool RecoveryDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_isVisible) return false;
    
    if (event.pressed) {
        if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
            // Escape = Recover (safe default)
            handleResponse(RecoveryResponse::Recover);
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Enter) {
            // Enter = Recover (primary action)
            handleResponse(RecoveryResponse::Recover);
            return true;
        }
    }
    
    // Block all key events when visible
    return true;
}

void RecoveryDialog::handleResponse(RecoveryResponse response) {
    m_response = response;
    hide();
    
    if (m_callback) {
        m_callback(response);
    }
}

} // namespace Aestra
