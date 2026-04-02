// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ExportDialog.h"
#include "AudioDeviceManager.h"
#include "AudioExporter.h"
#include "../App/ServiceLocator.h"
#include "../../AestraPlat/include/AestraPlatform.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <cstdio>

ExportDialog::ExportDialog() {
    setId("exportDialog");
    setVisible(false);
}

ExportDialog::~ExportDialog() {
    if (m_exporting.load()) {
        m_cancelRequested.store(true);
    }
    if (m_exportFuture.valid()) {
        m_exportFuture.wait();
    }
    restoreAudioStreamIfNeeded();
}

void ExportDialog::show(const std::string& projectPath, Aestra::Audio::AudioEngine& engine, Aestra::Audio::TrackManager& trackManager) {
    m_projectPath = projectPath;
    m_engine = &engine;
    m_trackManager = &trackManager;
    m_panelState = PanelState::Settings;
    m_progress = 0.0f;
    m_exporting = false;
    m_cancelRequested = false;
    m_exportError.clear();
    m_exportElapsed = 0.0;

    // Defaults from engine
    uint32_t sr = engine.getSampleRate();
    m_selectedSampleRate = 1; // 48000
    if (sr == 44100) m_selectedSampleRate = 0;
    else if (sr == 88200) m_selectedSampleRate = 2;
    else if (sr == 96000) m_selectedSampleRate = 3;

    // Default output path
    std::string exportName = Aestra::Audio::AudioExporter::getDefaultExportName(projectPath);
    std::filesystem::path outDir = std::filesystem::path(projectPath).parent_path();
    if (outDir.empty()) outDir = std::filesystem::current_path();
    m_outputPath = (outDir / exportName).string();
    syncTailInputFromValue();

    m_visible = true;
    setVisible(true);
    layoutDialog();
}

void ExportDialog::hide() {
    m_visible = false;
    setVisible(false);
    m_tailInputFocused = false;
    if (m_exporting.load()) {
        m_cancelRequested.store(true);
        // Non-blocking: destructor will join the future when needed
    } else {
        restoreAudioStreamIfNeeded();
    }
}

void ExportDialog::layoutDialog() {
    auto parentBounds = getBounds();
    m_dialogRect.x = parentBounds.x + (parentBounds.width - DIALOG_WIDTH) * 0.5f;
    m_dialogRect.y = parentBounds.y + (parentBounds.height - DIALOG_HEIGHT) * 0.5f;
    m_dialogRect.width = DIALOG_WIDTH;
    m_dialogRect.height = DIALOG_HEIGHT;

    float pad = 24.0f;
    float contentX = m_dialogRect.x + pad;
    float contentW = m_dialogRect.width - pad * 2.0f;
    float rowH = 30.0f;
    float labelW = 110.0f;
    float gap = 12.0f;
    float y = m_dialogRect.y + 56.0f; // Below title

    if (m_panelState == PanelState::Settings) {
        // Output path row
        m_outputFieldRect = AestraUI::NUIRect(contentX + labelW, y, contentW - labelW - 82.0f, rowH);
        m_browseButtonRect = AestraUI::NUIRect(m_outputFieldRect.right() + 10.0f, y, 72.0f, rowH);
        y += rowH + gap;

        // Format section
        m_scopeDropdownRect = AestraUI::NUIRect(contentX + labelW, y, contentW - labelW, rowH);
        m_scopeDropdownPopup = m_scopeDropdownRect;
        y += rowH + gap;

        m_sampleRateDropdownRect = AestraUI::NUIRect(contentX + labelW, y, contentW - labelW, rowH);
        m_sampleRateDropdownPopup = m_sampleRateDropdownRect;
        y += rowH + gap;

        m_bitDepthDropdownRect = AestraUI::NUIRect(contentX + labelW, y, contentW - labelW, rowH);
        m_bitDepthDropdownPopup = m_bitDepthDropdownRect;
        y += rowH + gap;

        // Tail row
        m_tailInputRect = AestraUI::NUIRect(contentX + labelW, y, 80.0f, rowH);

        // Buttons at bottom
        float btnW = 110.0f;
        float btnH = 36.0f;
        float btnY = m_dialogRect.bottom() - btnH - 18.0f;
        float totalBtnW = btnW * 2.0f + 12.0f;
        float btnStartX = m_dialogRect.x + (m_dialogRect.width - totalBtnW) * 0.5f;
        m_startButtonRect = AestraUI::NUIRect(btnStartX, btnY, btnW, btnH);
        m_closeButtonRect = AestraUI::NUIRect(btnStartX + btnW + 12.0f, btnY, btnW, btnH);
    } else {
        // Progress/Complete state
        float btnW = 110.0f;
        float btnH = 36.0f;
        float btnY = m_dialogRect.y + m_dialogRect.height - btnH - 20.0f;
        float totalBtnW = btnW + (m_panelState == PanelState::Progress ? btnW + 12.0f : 0.0f);
        float btnStartX = m_dialogRect.x + (m_dialogRect.width - totalBtnW) * 0.5f;
        if (m_panelState == PanelState::Progress) {
            m_cancelButtonRect = AestraUI::NUIRect(btnStartX, btnY, btnW, btnH);
            m_closeButtonRect = AestraUI::NUIRect(btnStartX + btnW + 12.0f, btnY, btnW, btnH);
        } else {
            m_closeButtonRect = AestraUI::NUIRect(btnStartX, btnY, btnW, btnH);
        }
    }
}

void ExportDialog::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (m_panelState == PanelState::Progress && m_exporting) {
        m_exportElapsed += deltaTime;
    }

    if (m_exportFuture.valid() &&
        m_exportFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        applyExportResult(m_exportFuture.get());
    }
}

void ExportDialog::syncTailInputFromValue() {
    char tailBuf[32];
    std::snprintf(tailBuf, sizeof(tailBuf), "%.1f", m_tailSeconds);
    m_tailInput = tailBuf;
}

void ExportDialog::applyExportResult(const ExportJobResult& result) {
    m_exporting = false;
    m_panelState = PanelState::Complete;
    restoreAudioStreamIfNeeded();

    if (result.success) {
        m_exportResultPath = result.outputPath;
        m_exportDuration = result.durationSeconds;
        m_exportPeakDb = result.peakDb;
        m_exportError.clear();
    } else {
        m_exportResultPath.clear();
        m_exportDuration = 0.0;
        m_exportPeakDb = -96.0;
        m_exportError = result.errorMessage;
    }

    layoutDialog();
    setDirty(true);
}

void ExportDialog::restoreAudioStreamIfNeeded() {
    if (!m_resumeAudioStreamAfterExport) {
        return;
    }

    if (m_exporting.load()) {
        return;
    }
    if (m_exportFuture.valid() &&
        m_exportFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    if (auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>()) {
        deviceManager->startStream();
    }
    m_resumeAudioStreamAfterExport = false;
}

bool ExportDialog::parseTailInput(double& outTailSeconds) const {
    if (m_tailInput.empty()) {
        return false;
    }

    char* endPtr = nullptr;
    const double parsed = std::strtod(m_tailInput.c_str(), &endPtr);
    if (endPtr == m_tailInput.c_str() || (endPtr && *endPtr != '\0') || !std::isfinite(parsed) || parsed < 0.0) {
        return false;
    }

    outTailSeconds = parsed;
    return true;
}

void ExportDialog::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    layoutDialog();
}

void ExportDialog::onRender(AestraUI::NUIRenderer& renderer) {
    if (!m_visible) return;
    drawOverlay(renderer);
    drawDialog(renderer);
}

void ExportDialog::drawOverlay(AestraUI::NUIRenderer& renderer) {
    auto parentBounds = getBounds();
    renderer.fillRect(parentBounds, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.72f));
}

void ExportDialog::drawDialog(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    AestraUI::NUIColor dialogBg = theme.getColor("surfaceTertiary");
    AestraUI::NUIColor border = theme.getColor("border").withAlpha(0.8f);

    // Shadow
    renderer.fillRoundedRect(AestraUI::NUIRect(m_dialogRect.x + 6, m_dialogRect.y + 10, m_dialogRect.width, m_dialogRect.height), 12.0f, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.32f));
    renderer.fillRoundedRect(m_dialogRect, 12.0f, dialogBg);
    renderer.strokeRoundedRect(m_dialogRect, 12.0f, 1.0f, border);

    // Title bar area
    float titleY = m_dialogRect.y + 14.0f;
    std::string title = (m_panelState == PanelState::Settings) ? "Export Audio" :
                        (m_panelState == PanelState::Progress) ? "Rendering..." : "Export Complete";
    renderer.drawText(title, AestraUI::NUIPoint(m_dialogRect.x + 20.0f, titleY), 16.0f, theme.getColor("textPrimary"));

    // Separator
    renderer.drawLine(AestraUI::NUIPoint(m_dialogRect.x + 16.0f, titleY + 24.0f),
                      AestraUI::NUIPoint(m_dialogRect.x + m_dialogRect.width - 16.0f, titleY + 24.0f),
                      1.0f, border);

    if (m_panelState == PanelState::Settings) {
        drawSettingsPanel(renderer);
    } else {
        drawProgressPanel(renderer);
    }
}

void ExportDialog::drawSettingsPanel(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    float pad = 24.0f;
    float contentX = m_dialogRect.x + pad;
    float labelW = 110.0f;
    float rowH = 30.0f;
    float gap = 12.0f;
    float y = m_dialogRect.y + 56.0f;
    const AestraUI::NUIColor fieldBg = theme.getColor("surfaceRaised");
    const AestraUI::NUIColor fieldBorder = theme.getColor("border").withAlpha(0.85f);
    const AestraUI::NUIColor fieldBorderActive = theme.getColor("primary").withAlpha(0.85f);

    auto drawLabel = [&](const std::string& text, float yPos) {
        renderer.drawText(text, AestraUI::NUIPoint(contentX, yPos + 7.0f), 12.0f, theme.getColor("textSecondary"));
    };

    auto drawDropdown = [&](const AestraUI::NUIRect& rect, const std::string& value, bool isOpen) {
        AestraUI::NUIColor borderCol = isOpen ? fieldBorderActive : fieldBorder;
        renderer.fillRoundedRect(rect, 6.0f, fieldBg);
        renderer.strokeRoundedRect(rect, 6.0f, 1.0f, borderCol);
        renderer.drawText(value, AestraUI::NUIPoint(rect.x + 8.0f, rect.y + 7.0f), 12.0f, theme.getColor("textPrimary"));
        renderer.drawText(isOpen ? "^" : "v", AestraUI::NUIPoint(rect.x + rect.width - 16.0f, rect.y + 7.0f), 10.0f, theme.getColor("textSecondary"));
    };

    auto drawDropdownPopup = [&](const AestraUI::NUIRect& popup, const std::vector<std::string>& options, int selected) {
        float itemH = 26.0f;
        float popupH = options.size() * itemH + 4.0f;
        AestraUI::NUIRect actualPopup = popup;
        actualPopup.height = popupH;
        actualPopup.y = popup.y + popup.height + 2.0f;

        renderer.fillRoundedRect(actualPopup, 6.0f, fieldBg);
        renderer.strokeRoundedRect(actualPopup, 6.0f, 1.0f, fieldBorder);

        for (size_t i = 0; i < options.size(); i++) {
            AestraUI::NUIRect itemRect(actualPopup.x + 2.0f, actualPopup.y + 2.0f + i * itemH, actualPopup.width - 4.0f, itemH);
            AestraUI::NUIColor itemBg = (static_cast<int>(i) == selected) ? theme.getColor("primary").withAlpha(0.2f) : AestraUI::NUIColor(0,0,0,0);
            renderer.fillRoundedRect(itemRect, 3.0f, itemBg);
            renderer.drawText(options[i], AestraUI::NUIPoint(itemRect.x + 8.0f, itemRect.y + 7.0f), 12.0f, theme.getColor("textPrimary"));
        }
    };

    // Output path
    drawLabel("Output", y);
    renderer.fillRoundedRect(m_outputFieldRect, 6.0f, fieldBg);
    renderer.strokeRoundedRect(m_outputFieldRect, 6.0f, 1.0f, fieldBorder);
    std::string displayPath = m_outputPath;
    if (displayPath.size() > 42) displayPath = "..." + displayPath.substr(displayPath.size() - 39);
    renderer.drawText(displayPath, AestraUI::NUIPoint(m_outputFieldRect.x + 8.0f, m_outputFieldRect.y + 7.0f), 11.0f, theme.getColor("textPrimary"));
    {
        bool hovered = (m_hoveredButton == 3);
        AestraUI::NUIColor btnBg = hovered ? theme.getColor("hover") : fieldBg;
        renderer.fillRoundedRect(m_browseButtonRect, 6.0f, btnBg);
        renderer.strokeRoundedRect(m_browseButtonRect, 6.0f, 1.0f, fieldBorder);
        renderer.drawTextCentered("Browse", m_browseButtonRect, 11.0f, theme.getColor("textPrimary"));
    }
    y += rowH + gap;

    // Scope
    drawLabel("Scope", y);
    drawDropdown(m_scopeDropdownRect, m_scopeOptions[m_selectedScope], m_scopeOpen);
    y += rowH + gap;

    // Sample rate
    drawLabel("Sample Rate", y);
    drawDropdown(m_sampleRateDropdownRect, m_sampleRateOptions[m_selectedSampleRate], m_sampleRateOpen);
    y += rowH + gap;

    // Bit depth
    drawLabel("Bit Depth", y);
    drawDropdown(m_bitDepthDropdownRect, m_bitDepthOptions[m_selectedBitDepth], m_bitDepthOpen);
    y += rowH + gap;

    // Tail
    drawLabel("Tail (seconds)", y);
    {
        const AestraUI::NUIColor tailBorder = m_tailInputFocused ? fieldBorderActive : fieldBorder;
        renderer.fillRoundedRect(m_tailInputRect, 6.0f, fieldBg);
        renderer.strokeRoundedRect(m_tailInputRect, 6.0f, 1.0f, tailBorder);
        renderer.drawText(m_tailInput, AestraUI::NUIPoint(m_tailInputRect.x + 8.0f, m_tailInputRect.y + 7.0f), 12.0f, theme.getColor("textPrimary"));
    }

    // Buttons
    {
        bool startHovered = (m_hoveredButton == 0);
        AestraUI::NUIColor startBg = startHovered ? theme.getColor("primary").withAlpha(0.8f) : theme.getColor("primary");
        renderer.fillRoundedRect(m_startButtonRect, 6.0f, startBg);
        renderer.drawTextCentered("Export", m_startButtonRect, 13.0f, theme.getColor("backgroundPrimary"));

        bool closeHovered = (m_hoveredButton == 2);
        AestraUI::NUIColor closeBg = closeHovered ? theme.getColor("hover") : fieldBg;
        renderer.fillRoundedRect(m_closeButtonRect, 6.0f, closeBg);
        renderer.strokeRoundedRect(m_closeButtonRect, 6.0f, 1.0f, fieldBorder);
        renderer.drawTextCentered("Cancel", m_closeButtonRect, 13.0f, theme.getColor("textPrimary"));
    }

    if (m_scopeOpen) {
        drawDropdownPopup(m_scopeDropdownPopup, m_scopeOptions, m_selectedScope);
    } else if (m_sampleRateOpen) {
        drawDropdownPopup(m_sampleRateDropdownPopup, m_sampleRateOptions, m_selectedSampleRate);
    } else if (m_bitDepthOpen) {
        drawDropdownPopup(m_bitDepthDropdownPopup, m_bitDepthOptions, m_selectedBitDepth);
    }
}

void ExportDialog::drawProgressPanel(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    float pad = 24.0f;
    float contentX = m_dialogRect.x + pad;
    float contentW = m_dialogRect.width - pad * 2.0f;
    float y = m_dialogRect.y + 52.0f;
    const AestraUI::NUIColor fieldBg = theme.getColor("surfaceRaised");
    const AestraUI::NUIColor fieldBorder = theme.getColor("border").withAlpha(0.85f);

    if (m_panelState == PanelState::Progress) {
        // Status text
        std::string status = "Rendering audio...";
        renderer.drawText(status, AestraUI::NUIPoint(contentX, y), 13.0f, theme.getColor("textPrimary"));
        y += 24.0f;

        // Progress bar (manual draw for full control)
        float barH = 20.0f;
        AestraUI::NUIRect barRect(contentX, y, contentW, barH);
        renderer.fillRoundedRect(barRect, 4.0f, fieldBg);

        float progress = std::max(0.0f, std::min(1.0f, m_progress.load()));
        if (progress > 0.0f) {
            float fillW = std::max(4.0f, contentW * progress);
            AestraUI::NUIRect fillRect(contentX, y, fillW, barH);
            renderer.fillRoundedRect(fillRect, 4.0f, theme.getColor("primary"));
        }

        // Progress text
        int pct = static_cast<int>(progress * 100.0f);
        std::string pctText = std::to_string(pct) + "%";
        auto textSize = renderer.measureText(pctText, 11.0f);
        float textX = contentX + (contentW - textSize.width) * 0.5f;
        renderer.drawText(pctText, AestraUI::NUIPoint(textX, y + 4.0f), 11.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
        y += barH + 16.0f;

        // Elapsed time
        int elapsedMin = static_cast<int>(m_exportElapsed) / 60;
        int elapsedSec = static_cast<int>(m_exportElapsed) % 60;
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "Elapsed: %d:%02d", elapsedMin, elapsedSec);
        renderer.drawText(timeBuf, AestraUI::NUIPoint(contentX, y), 11.0f, theme.getColor("textSecondary"));

        // Buttons
        {
            bool cancelHovered = (m_hoveredButton == 1);
            AestraUI::NUIColor cancelBg = cancelHovered ? AestraUI::NUIColor(0.8f, 0.2f, 0.2f, 0.8f) : AestraUI::NUIColor(0.6f, 0.15f, 0.15f, 1.0f);
            renderer.fillRoundedRect(m_cancelButtonRect, 6.0f, cancelBg);
            renderer.drawTextCentered("Cancel", m_cancelButtonRect, 13.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));

            bool closeHovered = (m_hoveredButton == 2);
            AestraUI::NUIColor closeBg = closeHovered ? theme.getColor("hover") : fieldBg;
            renderer.fillRoundedRect(m_closeButtonRect, 6.0f, closeBg);
            renderer.strokeRoundedRect(m_closeButtonRect, 6.0f, 1.0f, fieldBorder);
            renderer.drawTextCentered("Close", m_closeButtonRect, 13.0f, theme.getColor("textPrimary"));
        }
    } else if (m_panelState == PanelState::Complete) {
        // Result
        if (!m_exportError.empty()) {
            renderer.drawText("Export Failed", AestraUI::NUIPoint(contentX, y), 14.0f, AestraUI::NUIColor(0.9f, 0.3f, 0.3f, 1.0f));
            y += 24.0f;
            renderer.drawText(m_exportError, AestraUI::NUIPoint(contentX, y), 12.0f, theme.getColor("textSecondary"));
        } else {
            renderer.drawText("Export Complete", AestraUI::NUIPoint(contentX, y), 14.0f, theme.getColor("success"));
            y += 24.0f;

            // Output path
            renderer.drawText("Output:", AestraUI::NUIPoint(contentX, y), 11.0f, theme.getColor("textSecondary"));
            y += 16.0f;
            std::string displayPath = m_exportResultPath;
            if (displayPath.size() > 50) displayPath = "..." + displayPath.substr(displayPath.size() - 47);
            renderer.drawText(displayPath, AestraUI::NUIPoint(contentX, y), 11.0f, theme.getColor("textPrimary"));
            y += 18.0f;

            // Stats
            char statsBuf[128];
            snprintf(statsBuf, sizeof(statsBuf), "Duration: %.1fs  |  Peak: %.1f dB", m_exportDuration, m_exportPeakDb);
            renderer.drawText(statsBuf, AestraUI::NUIPoint(contentX, y), 11.0f, theme.getColor("textSecondary"));
        }

        y += 32.0f;

        // Close button
        bool closeHovered = (m_hoveredButton == 2);
        AestraUI::NUIColor closeBg = closeHovered ? theme.getColor("primary").withAlpha(0.8f) : theme.getColor("primary");
        renderer.fillRoundedRect(m_closeButtonRect, 6.0f, closeBg);
        renderer.drawTextCentered(m_exportError.empty() ? "Done" : "Close", m_closeButtonRect, 13.0f, AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
    }
}

bool ExportDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_visible) return false;

    updateButtonHover(event.position);

    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        handleMouseClick(event.position);
        return true;
    }

    return true; // Modal
}

bool ExportDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_visible) return false;
    if (!event.pressed) return true;

    if (m_tailInputFocused) {
        if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
            m_tailInputFocused = false;
            syncTailInputFromValue();
            setDirty(true);
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Enter) {
            double parsedTail = 0.0;
            if (parseTailInput(parsedTail)) {
                m_tailSeconds = parsedTail;
            } else {
                syncTailInputFromValue();
            }
            m_tailInputFocused = false;
            setDirty(true);
            return true;
        }
        if (event.keyCode == AestraUI::NUIKeyCode::Backspace) {
            if (!m_tailInput.empty()) {
                m_tailInput.pop_back();
                setDirty(true);
            }
            return true;
        }

        const char c = event.character;
        if ((c >= '0' && c <= '9') || (c == '.' && m_tailInput.find('.') == std::string::npos)) {
            m_tailInput.push_back(c);
            setDirty(true);
        }
        return true;
    }

    if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
        if (m_panelState == PanelState::Progress && m_exporting) {
            m_cancelRequested = true;
        } else {
            hide();
        }
        return true;
    }
    return true; // Modal
}

void ExportDialog::handleMouseClick(AestraUI::NUIPoint pos) {
    // Check dropdown popups first
    if (m_scopeOpen) {
        float itemH = 26.0f;
        float popupH = m_scopeOptions.size() * itemH + 4.0f;
        AestraUI::NUIRect popup = m_scopeDropdownRect;
        popup.height = popupH;
        popup.y = m_scopeDropdownRect.y + m_scopeDropdownRect.height + 2.0f;
        if (popup.contains(pos)) {
            int idx = static_cast<int>((pos.y - popup.y - 2.0f) / itemH);
            if (idx >= 0 && idx < static_cast<int>(m_scopeOptions.size())) {
                m_selectedScope = idx;
                layoutDialog();
            }
        }
        m_scopeOpen = false;
        return;
    }
    if (m_sampleRateOpen) {
        float itemH = 26.0f;
        float popupH = m_sampleRateOptions.size() * itemH + 4.0f;
        AestraUI::NUIRect popup = m_sampleRateDropdownRect;
        popup.height = popupH;
        popup.y = m_sampleRateDropdownRect.y + m_sampleRateDropdownRect.height + 2.0f;
        if (popup.contains(pos)) {
            int idx = static_cast<int>((pos.y - popup.y - 2.0f) / itemH);
            if (idx >= 0 && idx < static_cast<int>(m_sampleRateOptions.size())) {
                m_selectedSampleRate = idx;
                layoutDialog();
            }
        }
        m_sampleRateOpen = false;
        return;
    }
    if (m_bitDepthOpen) {
        float itemH = 26.0f;
        float popupH = m_bitDepthOptions.size() * itemH + 4.0f;
        AestraUI::NUIRect popup = m_bitDepthDropdownRect;
        popup.height = popupH;
        popup.y = m_bitDepthDropdownRect.y + m_bitDepthDropdownRect.height + 2.0f;
        if (popup.contains(pos)) {
            int idx = static_cast<int>((pos.y - popup.y - 2.0f) / itemH);
            if (idx >= 0 && idx < static_cast<int>(m_bitDepthOptions.size())) {
                m_selectedBitDepth = idx;
                layoutDialog();
            }
        }
        m_bitDepthOpen = false;
        return;
    }

    // Close dropdowns on click outside
    m_scopeOpen = false;
    m_sampleRateOpen = false;
    m_bitDepthOpen = false;

    // Dropdown toggles
    if (m_scopeDropdownRect.contains(pos)) { m_scopeOpen = !m_scopeOpen; return; }
    if (m_sampleRateDropdownRect.contains(pos)) { m_sampleRateOpen = !m_sampleRateOpen; return; }
    if (m_bitDepthDropdownRect.contains(pos)) { m_bitDepthOpen = !m_bitDepthOpen; return; }
    if (m_tailInputRect.contains(pos)) {
        m_tailInputFocused = true;
        setDirty(true);
        return;
    }
    m_tailInputFocused = false;

    // Browse button
    if (m_browseButtonRect.contains(pos)) {
        if (auto* utils = Aestra::Platform::getUtils()) {
            Aestra::IPlatformUtils::SaveFileDialogOptions options;
            options.title = "Export Audio";
            options.filter = std::string("WAV Files\0*.wav\0All Files\0*.*\0",
                                         sizeof("WAV Files\0*.wav\0All Files\0*.*\0") - 1);
            options.defaultPath = m_outputPath;
            options.defaultExtension = "wav";
            const std::string pickedPath = utils->saveFileDialog(options);
            if (!pickedPath.empty()) {
                m_outputPath = pickedPath;
                setDirty(true);
            }
        }
        return;
    }

    // Start/Export button
    if (m_startButtonRect.contains(pos) && m_panelState == PanelState::Settings) {
        startExport();
        return;
    }

    // Cancel button
    if (m_cancelButtonRect.contains(pos) && m_panelState == PanelState::Progress) {
        m_cancelRequested = true;
        return;
    }

    // Close button
    if (m_closeButtonRect.contains(pos)) {
        if (m_panelState == PanelState::Complete) {
            hide();
            if (m_onExportComplete) m_onExportComplete(m_exportError.empty(), m_exportResultPath, m_exportDuration, m_exportPeakDb);
        } else if (m_panelState == PanelState::Settings) {
            hide();
        }
        return;
    }
}

void ExportDialog::updateButtonHover(AestraUI::NUIPoint pos) {
    int prev = m_hoveredButton;
    m_hoveredButton = -1;

    if (m_panelState == PanelState::Settings) {
        if (m_startButtonRect.contains(pos)) m_hoveredButton = 0;
        else if (m_closeButtonRect.contains(pos)) m_hoveredButton = 2;
        else if (m_browseButtonRect.contains(pos)) m_hoveredButton = 3;
    } else if (m_panelState == PanelState::Progress) {
        if (m_cancelButtonRect.contains(pos)) m_hoveredButton = 1;
        else if (m_closeButtonRect.contains(pos)) m_hoveredButton = 2;
    } else {
        if (m_closeButtonRect.contains(pos)) m_hoveredButton = 2;
    }

    if (prev != m_hoveredButton) setDirty(true);
}

void ExportDialog::startExport() {
    if (!m_engine || !m_trackManager) return;

    double parsedTail = 0.0;
    if (!parseTailInput(parsedTail)) {
        m_panelState = PanelState::Complete;
        m_exportError = "Invalid tail value";
        m_exportResultPath.clear();
        m_exportDuration = 0.0;
        m_exportPeakDb = -96.0;
        layoutDialog();
        setDirty(true);
        return;
    }

    if (m_outputPath.empty()) {
        m_panelState = PanelState::Complete;
        m_exportError = "No output path specified";
        m_exportResultPath.clear();
        m_exportDuration = 0.0;
        m_exportPeakDb = -96.0;
        layoutDialog();
        setDirty(true);
        return;
    }

    m_tailSeconds = parsedTail;
    m_tailInputFocused = false;

    m_panelState = PanelState::Progress;
    m_progress = 0.0f;
    m_exporting = true;
    m_cancelRequested = false;
    m_exportError.clear();
    m_exportElapsed = 0.0f;
    layoutDialog();

    if (auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>()) {
        m_resumeAudioStreamAfterExport = deviceManager->isStreamRunning();
        if (m_resumeAudioStreamAfterExport) {
            deviceManager->stopStream();
        }
    } else {
        m_resumeAudioStreamAfterExport = false;
    }

    try {
        m_exportFuture = std::async(std::launch::async,
            &ExportDialog::exportThreadFn, this,
            m_outputPath, m_selectedBitDepth, m_selectedSampleRate, m_selectedScope, m_tailSeconds);
    } catch (const std::system_error& e) {
        if (m_resumeAudioStreamAfterExport) {
            if (auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>()) {
                deviceManager->startStream();
            }
            m_resumeAudioStreamAfterExport = false;
        }
        m_exporting = false;
        m_panelState = PanelState::Complete;
        m_exportError = std::string("Failed to start export worker: ") + e.what();
        Aestra::Log::error("[ExportDialog] " + m_exportError);
        layoutDialog();
        setDirty(true);
    } catch (const std::exception& e) {
        if (m_resumeAudioStreamAfterExport) {
            if (auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>()) {
                deviceManager->startStream();
            }
            m_resumeAudioStreamAfterExport = false;
        }
        m_exporting = false;
        m_panelState = PanelState::Complete;
        m_exportError = std::string("Failed to start export worker: ") + e.what();
        Aestra::Log::error("[ExportDialog] " + m_exportError);
        layoutDialog();
        setDirty(true);
    }
}

ExportDialog::ExportJobResult ExportDialog::exportThreadFn(std::string outputPath, int selectedBitDepth, int selectedSampleRate, int selectedScope, double tailSeconds) {
    Aestra::Audio::AudioExporter::Config config;
    config.outputPath = std::move(outputPath);
    config.sampleRate = std::stoi(m_sampleRateOptions[selectedSampleRate]);
    config.numChannels = 2;
    config.tailSeconds = tailSeconds;

    switch (selectedBitDepth) {
        case 0: config.bitDepth = Aestra::Audio::AudioExporter::BitDepth::PCM_16; break;
        case 1: config.bitDepth = Aestra::Audio::AudioExporter::BitDepth::PCM_24; break;
        case 2: config.bitDepth = Aestra::Audio::AudioExporter::BitDepth::Float_32; break;
    }

    switch (selectedScope) {
        case 0: config.scope = Aestra::Audio::AudioExporter::RenderScope::FullSong; break;
        case 1: config.scope = Aestra::Audio::AudioExporter::RenderScope::LoopRegion; break;
        case 2: config.scope = Aestra::Audio::AudioExporter::RenderScope::Selection; break;
    }

    Aestra::Audio::AudioExporter exporter(*m_engine, *m_trackManager);
    exporter.setProgressCallback([this](float pct) {
        m_progress.store(pct, std::memory_order_relaxed);
    });
    exporter.setCancelCheck([this]() {
        return m_cancelRequested.load(std::memory_order_relaxed);
    });

    auto result = exporter.render(config);
    return ExportJobResult{
        .success = result.success,
        .outputPath = result.outputPath,
        .durationSeconds = result.durationSeconds,
        .peakDb = result.peakDb,
        .errorMessage = result.errorMessage,
    };
}
