// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraFileDialog.h"
#include "IO/EncodedAudioWriter.h"
#include "NUIButton.h"
#include "NUICheckbox.h"
#include "NUIComponent.h"
#include "NUIDropdown.h"
#include "NUILabel.h"
#include "NUIProgressBar.h"
#include "NUITextInput.h"

#include <array>
#include <atomic>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <thread>
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
        /** @brief Files written: 1 for a mix, one per track plus the master for stems. */
        int fileCount = 1;
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
    /** @brief True when the board is set to stems + master. */
    bool exportsStems() const { return m_stems; }

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
    enum class PanelState { Settings, Progress, Complete };

    void buildControls();
    void layoutDialog();
    void applyPanelState();
    void startExport();
    Aestra::Audio::ExportFormat formatOf(int optionIndex) const; ///< WAV, FLAC or MP3 for a Format menu entry
    ExportJobResult exportThreadFn(std::string outputPath, int selectedBitDepth, int selectedSampleRate, int selectedScope, double tailSeconds, bool stems);
    void openBrowse();
    bool parseTailInput(double& outTailSeconds) const;
    void syncTailInputFromValue();
    void applyExportResult(const ExportJobResult& result);
    void restoreAudioStreamIfNeeded();
    void failToComplete(const std::string& message);
    void finish();

    PanelState m_panelState = PanelState::Settings;

    bool m_visible = false;
    AestraUI::NUIRect m_dialogRect;
    static constexpr float DIALOG_WIDTH = 560.0f; // canvas-2 board 3
    static constexpr float DIALOG_HEIGHT = 392.0f;

    // Settings
    std::string m_outputPath;
    std::string m_projectPath;
    int m_selectedBitDepth = 1;
    int m_selectedSampleRate = 1;
    int m_selectedScope = 0;
    bool m_stems = false;
    double m_tailSeconds = 2.0;
    std::string m_tailInput;

    // The Format menu: the three WAV depths keep indices 0-2 (saved settings still mean the same), then FLAC and MP3
    // when their encoders are on this computer (EncodedAudioWriter, FD-61).
    std::vector<std::string> m_bitDepthOptions = {"WAV 16-bit", "WAV 24-bit", "WAV 32-bit float"};
    std::vector<std::string> m_sampleRateOptions = {"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"};
    static constexpr uint32_t kSampleRates[4] = {44100, 48000, 88200, 96000}; ///< what each rate entry renders at
    std::vector<std::string> m_scopeOptions = { "Full Song", "Loop Region", "Selection" };

    // Progress
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_exporting{false};
    std::atomic<bool> m_cancelRequested{false};
    std::future<ExportJobResult> m_exportFuture;
    // Browse picker: off the UI thread, never waited on (see AestraFileDialog.h).
    Aestra::PendingFileDialog m_fileDialog;
    std::string m_exportResultPath;
    int m_exportFileCount = 1;
    double m_exportDuration = 0.0;
    double m_exportPeakDb = -96.0;
    std::string m_exportError;
    double m_exportElapsed = 0.0;
    bool m_resumeAudioStreamAfterExport = false;

    // Engine refs
    Aestra::Audio::AudioEngine* m_engine = nullptr;
    Aestra::Audio::TrackManager* m_trackManager = nullptr;

    // Controls. Settings: rows of label + control. Progress and Complete reuse the
    // status labels and the buttons.
    std::shared_ptr<AestraUI::NUILabel> m_outputLabel, m_scopeLabel, m_rateLabel, m_depthLabel, m_tailLabel;
    std::shared_ptr<AestraUI::NUITextInput> m_outputInput, m_tailField;
    std::shared_ptr<AestraUI::NUIButton> m_browseButton;
    std::shared_ptr<AestraUI::NUIDropdown> m_scopeDropdown, m_rateDropdown, m_depthDropdown;
    std::shared_ptr<AestraUI::NUICheckbox> m_stemsCheckbox;
    std::shared_ptr<AestraUI::NUILabel> m_stemsHint;
    std::shared_ptr<AestraUI::NUILabel> m_statusLabel, m_detailLabel, m_detailLabel2, m_elapsedLabel;
    std::shared_ptr<AestraUI::NUIProgressBar> m_progressBar;
    std::shared_ptr<AestraUI::NUIButton> m_exportButton, m_cancelButton, m_doneButton;

    ExportCallback m_onExportComplete;

    // Board 3 (V82-P12, ExportDialogBoard.cpp): what (scope, one file or stems), where (name + folder), format chips,
    // and a summary line worked out before Export.
    std::array<AestraUI::NUIRect, 3> m_scopeSegments{};
    std::array<AestraUI::NUIRect, 2> m_stemsSegments{};
    AestraUI::NUIRect m_whatCaption, m_whereCaption, m_formatCaption, m_pathLine, m_summaryLine;
    std::shared_ptr<AestraUI::NUIDropdown> m_formatDropdown;
    std::vector<Aestra::Audio::ExportFormat> m_formats; ///< the Format menu's entries, in order
    Aestra::Audio::ExportFormat m_format = Aestra::Audio::ExportFormat::Wav;
    std::string m_outputFolder;
    void buildBoardControls();
    void layoutSettingsBoard();
    void paintSettingsBoard(AestraUI::NUIRenderer& renderer);
    void paintSettingsBoardOverlay(AestraUI::NUIRenderer& renderer);
    bool handleBoardClick(const AestraUI::NUIMouseEvent& event);
    void setFormat(Aestra::Audio::ExportFormat format, int depthChoice);
    void syncFormatChips();
    void setOutputFromPath(const std::string& path);
    void refreshOutputPath();
    std::string summaryText() const;
};
