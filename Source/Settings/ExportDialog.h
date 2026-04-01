// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIButton.h"
#include "NUIProgressBar.h"
#include "NUIDropdown.h"
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <future>
#include <optional>
#include <vector>

namespace Aestra { namespace Audio { class AudioEngine; class TrackManager; } }

class ExportDialog : public AestraUI::NUIComponent {
public:
    struct ExportJobResult {
        bool success = false;
        std::string outputPath;
        double durationSeconds = 0.0;
        double peakDb = -96.0;
        std::string errorMessage;
    };

    ExportDialog();
    ~ExportDialog();

    void show(const std::string& projectPath, Aestra::Audio::AudioEngine& engine, Aestra::Audio::TrackManager& trackManager);
    void hide();
    bool isVisible() const { return m_visible; }

    using ExportCallback = std::function<void(bool success, const std::string& path, double duration, double peakDb)>;
    void setOnExportComplete(ExportCallback cb) { m_onExportComplete = cb; }

    void onUpdate(double deltaTime) override;
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;

private:
    void layoutDialog();
    void drawOverlay(AestraUI::NUIRenderer& renderer);
    void drawDialog(AestraUI::NUIRenderer& renderer);
    void drawSettingsPanel(AestraUI::NUIRenderer& renderer);
    void drawProgressPanel(AestraUI::NUIRenderer& renderer);
    void startExport();
    ExportJobResult exportThreadFn(std::string outputPath, int selectedBitDepth, int selectedSampleRate, int selectedScope, double tailSeconds);
    void handleMouseClick(AestraUI::NUIPoint pos);
    void updateButtonHover(AestraUI::NUIPoint pos);
    bool parseTailInput(double& outTailSeconds) const;
    void syncTailInputFromValue();
    void applyExportResult(const ExportJobResult& result);

    enum class PanelState { Settings, Progress, Complete };
    PanelState m_panelState = PanelState::Settings;

    bool m_visible = false;
    AestraUI::NUIRect m_dialogRect;
    static constexpr float DIALOG_WIDTH = 520.0f;
    static constexpr float DIALOG_HEIGHT = 330.0f;

    // Settings
    std::string m_outputPath;
    std::string m_projectPath;
    int m_selectedBitDepth = 1;
    int m_selectedSampleRate = 1;
    int m_selectedScope = 0;
    double m_tailSeconds = 2.0;
    std::string m_tailInput;

    std::vector<std::string> m_bitDepthOptions = { "16-bit PCM", "24-bit PCM", "32-bit Float" };
    std::vector<std::string> m_sampleRateOptions = { "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" };
    std::vector<std::string> m_scopeOptions = { "Full Song", "Loop Region", "Selection" };

    // Progress
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_exporting{false};
    std::atomic<bool> m_cancelRequested{false};
    std::future<ExportJobResult> m_exportFuture;
    std::string m_exportResultPath;
    double m_exportDuration = 0.0;
    double m_exportPeakDb = -96.0;
    std::string m_exportError;
    double m_exportElapsed = 0.0;

    // Engine refs
    Aestra::Audio::AudioEngine* m_engine = nullptr;
    Aestra::Audio::TrackManager* m_trackManager = nullptr;

    // UI state
    AestraUI::NUIRect m_startButtonRect;
    AestraUI::NUIRect m_cancelButtonRect;
    AestraUI::NUIRect m_closeButtonRect;
    AestraUI::NUIRect m_browseButtonRect;
    AestraUI::NUIRect m_outputFieldRect;
    AestraUI::NUIRect m_bitDepthDropdownRect;
    AestraUI::NUIRect m_sampleRateDropdownRect;
    AestraUI::NUIRect m_scopeDropdownRect;
    AestraUI::NUIRect m_tailInputRect;

    int m_hoveredButton = -1;
    bool m_bitDepthOpen = false;
    bool m_sampleRateOpen = false;
    bool m_scopeOpen = false;
    bool m_tailInputFocused = false;
    AestraUI::NUIRect m_bitDepthDropdownPopup;
    AestraUI::NUIRect m_sampleRateDropdownPopup;
    AestraUI::NUIRect m_scopeDropdownPopup;

    ExportCallback m_onExportComplete;
};
