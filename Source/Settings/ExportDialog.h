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
    /**
     * @brief Immutable result returned by the asynchronous export worker.
     */
    struct ExportJobResult {
        /** @brief True when the export completed successfully. */
        bool success = false;
        /** @brief Path to the exported file on success. */
        std::string outputPath;
        /** @brief Final rendered duration in seconds. */
        double durationSeconds = 0.0;
        /** @brief Peak level measured during export in dBFS. */
        double peakDb = -96.0;
        /** @brief Failure message when @ref success is false. */
        std::string errorMessage;
    };

    /**
     * @brief Create the export dialog widget.
     */
    ExportDialog();
    /**
     * @brief Cancel and join any in-flight export job before destruction.
     */
    ~ExportDialog();

    /**
     * @brief Show the dialog and bind it to a project and engine context.
     * @param projectPath Project path used to derive the default export name.
     * @param engine Audio engine used for offline rendering.
     * @param trackManager Track manager that provides playlist and pattern state.
     */
    void show(const std::string& projectPath, Aestra::Audio::AudioEngine& engine, Aestra::Audio::TrackManager& trackManager);
    /**
     * @brief Hide the dialog and request cancellation of any active export.
     */
    void hide();
    /**
     * @brief Check whether the dialog is currently visible.
     * @return True when the dialog is shown.
     */
    bool isVisible() const { return m_visible; }

    using ExportCallback = std::function<void(bool success, const std::string& path, double duration, double peakDb)>;
    /**
     * @brief Set the completion callback fired on the UI thread after export.
     * @param cb Completion callback with success flag and render metadata.
     */
    void setOnExportComplete(ExportCallback cb) { m_onExportComplete = cb; }

    /**
     * @brief Poll background export state and update the active panel.
     * @param deltaTime Frame delta in seconds.
     */
    void onUpdate(double deltaTime) override;
    /**
     * @brief Render the modal overlay and active export panel.
     * @param renderer Active UI renderer.
     */
    void onRender(AestraUI::NUIRenderer& renderer) override;
    /**
     * @brief Relayout the dialog after its parent view changes size.
     * @param width New parent width in logical pixels.
     * @param height New parent height in logical pixels.
     */
    void onResize(int width, int height) override;
    /**
     * @brief Handle mouse interaction with dialog controls and dropdowns.
     * @param event Pointer event routed to the dialog.
     * @return True when the event was consumed.
     */
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    /**
     * @brief Handle keyboard interaction for inputs and dismissal.
     * @param event Key event routed to the dialog.
     * @return True when the event was consumed.
     */
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
