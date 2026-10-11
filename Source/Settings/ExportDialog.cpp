// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ExportDialog.h"
#include "../AestraUI/Widgets/AestraPanelWindow.h"
#include "AudioDeviceManager.h"
#include "AudioExporter.h"
#include "IO/StemExport.h"
#include "../App/ServiceLocator.h"
#include "../Core/ExportDialogPreferences.h"
#include "../Core/UISurfaceStore.h"
#include "../../AestraPlat/include/AestraPlatform.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Layout/NUILayoutAlgorithms.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <utility>

namespace {

using AestraUI::NUIRect;
using AestraUI::Layout::NUILocalRect;

NUIRect toRect(const NUILocalRect& r) { return NUIRect(r.x, r.y, r.width, r.height); }

std::string shortenPath(const std::string& path, size_t maxChars) {
    if (path.size() <= maxChars)
        return path;
    return "..." + path.substr(path.size() - (maxChars - 3));
}

} // namespace

ExportDialog::ExportDialog() {
    setId("exportDialog");
    setVisible(false);
    buildControls();
}

ExportDialog::~ExportDialog() {
    if (m_exporting.load()) {
        m_cancelRequested.store(true);
    }
    if (m_exportFuture.valid()) {
        m_exportFuture.wait();
    }
    // An open Browse picker is not waited for: its worker owns its own state
    // and the late result is dropped. Waiting here hung window close until
    // the user dismissed the picker.
    restoreAudioStreamIfNeeded();
}

void ExportDialog::buildControls() {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const float fontM = theme.getCurrentTheme().fontSizeM;
    const auto makeLabel = [&](const char* text) {
        auto label = std::make_shared<AestraUI::NUILabel>();
        label->setText(text);
        label->setFontSize(fontM);
        label->setTextColor(theme.getColor("textSecondary"));
        addChild(label);
        return label;
    };
    const auto makeDropdown = [&](const std::vector<std::string>& options, std::function<void(int)> onPick) {
        auto dropdown = std::make_shared<AestraUI::NUIDropdown>();
        for (size_t i = 0; i < options.size(); ++i)
            dropdown->addItem(options[i], static_cast<int>(i));
        dropdown->setOnSelectionChanged(std::move(onPick));
        addChild(dropdown);
        return dropdown;
    };

    m_outputLabel = makeLabel("Output");
    m_outputInput = std::make_shared<AestraUI::NUITextInput>();
    m_outputInput->setOnTextChange([this](const std::string& text) { m_outputPath = text; });
    addChild(m_outputInput);
    m_browseButton = std::make_shared<AestraUI::NUIButton>("Browse");
    m_browseButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_browseButton->setOnClick([this]() { openBrowse(); });
    addChild(m_browseButton);

    m_scopeLabel = makeLabel("Scope");
    m_scopeDropdown = makeDropdown(m_scopeOptions, [this](int i) { m_selectedScope = i; });
    m_rateLabel = makeLabel("Sample Rate");
    m_rateDropdown = makeDropdown(m_sampleRateOptions, [this](int i) { m_selectedSampleRate = i; });
    m_depthLabel = makeLabel("Bit Depth");
    m_depthDropdown = makeDropdown(m_bitDepthOptions, [this](int i) { m_selectedBitDepth = i; });

    m_tailLabel = makeLabel("Tail (seconds)");
    m_tailField = std::make_shared<AestraUI::NUITextInput>();
    m_tailField->setInputType(AestraUI::NUITextInput::InputType::Number);
    m_tailField->setOnTextChange([this](const std::string& text) { m_tailInput = text; });
    m_tailField->setOnReturnKey([this]() {
        double parsed = 0.0;
        if (parseTailInput(parsed))
            m_tailSeconds = parsed;
        else
            syncTailInputFromValue();
        m_tailField->setText(m_tailInput);
    });
    addChild(m_tailField);

    m_stemsCheckbox = std::make_shared<AestraUI::NUICheckbox>("One file per track (stems), plus the master");
    m_stemsCheckbox->setOnCheckedChange([this](bool on) {
        m_stems = on;
        applyPanelState();
    });
    addChild(m_stemsCheckbox);
    m_stemsHint = makeLabel("");
    m_stemsHint->setFontSize(theme.getCurrentTheme().fontSizeS);
    m_stemsHint->setWordWrap(true);

    m_statusLabel = makeLabel("");
    m_statusLabel->setTextColor(theme.getColor("textPrimary"));
    m_detailLabel = makeLabel("");
    m_detailLabel->setWordWrap(true);
    m_detailLabel2 = makeLabel("");
    m_detailLabel2->setFontSize(theme.getCurrentTheme().fontSizeS);
    m_elapsedLabel = makeLabel("");
    m_elapsedLabel->setFontSize(theme.getCurrentTheme().fontSizeS);
    m_progressBar = std::make_shared<AestraUI::NUIProgressBar>();
    m_progressBar->setTextVisible(true);
    addChild(m_progressBar);

    m_exportButton = std::make_shared<AestraUI::NUIButton>("Export");
    m_exportButton->setStyle(AestraUI::NUIButton::Style::Primary);
    m_exportButton->setOnClick([this]() { startExport(); });
    addChild(m_exportButton);
    m_cancelButton = std::make_shared<AestraUI::NUIButton>("Cancel");
    m_cancelButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_cancelButton->setOnClick([this]() {
        if (m_panelState == PanelState::Progress)
            m_cancelRequested = true;
        else
            hide();
    });
    addChild(m_cancelButton);
    m_doneButton = std::make_shared<AestraUI::NUIButton>("Done");
    m_doneButton->setStyle(AestraUI::NUIButton::Style::Primary);
    m_doneButton->setOnClick([this]() { finish(); });
    addChild(m_doneButton);

    applyPanelState();
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
    syncTailInputFromValue();

    // V8-C14 step 5d: the stored `dialog.export` record wins per field over the
    // defaults computed above; anything the user never chose keeps the default.
    // Only the output *directory* is remembered — the file name always follows
    // the current project, or a rename would export under the old one.
    Aestra::ExportDialogValues live;
    live.sampleRateIndex = m_selectedSampleRate;
    live.bitDepthIndex = m_selectedBitDepth;
    live.scopeIndex = m_selectedScope;
    live.tailInput = m_tailInput;
    live.outputDirectory = outDir.string();

    if (const auto* store = Aestra::ServiceLocator::get<Aestra::UISurfaceStoreFile>()) {
        Aestra::ExportOptionCounts counts;
        counts.sampleRateCount = static_cast<int>(m_sampleRateOptions.size());
        counts.bitDepthCount = static_cast<int>(m_bitDepthOptions.size());
        counts.scopeCount = static_cast<int>(m_scopeOptions.size());
        Aestra::applyStoredExportOptions(live, store->dialogExportOptions(), counts, [](const std::string& dir) {
            std::error_code ec;
            return std::filesystem::is_directory(dir, ec);
        });
    }

    m_selectedSampleRate = live.sampleRateIndex;
    m_selectedBitDepth = live.bitDepthIndex;
    m_selectedScope = live.scopeIndex;
    m_outputPath = (std::filesystem::path(live.outputDirectory) / exportName).string();

    // The restored text is the authority for the tail value, but it is disk
    // data: an unparseable one falls back to the live seconds rather than
    // leaving the field and m_tailSeconds disagreeing.
    m_tailInput = live.tailInput;
    double restoredTail = 0.0;
    if (parseTailInput(restoredTail)) {
        m_tailSeconds = restoredTail;
    } else {
        syncTailInputFromValue();
    }

    m_scopeDropdown->setSelectedIndex(m_selectedScope);
    m_rateDropdown->setSelectedIndex(m_selectedSampleRate);
    m_depthDropdown->setSelectedIndex(m_selectedBitDepth);
    m_outputInput->setText(m_outputPath);
    m_tailField->setText(m_tailInput);
    m_stems = false;
    m_stemsCheckbox->setChecked(false);

    m_visible = true;
    setVisible(true);
    applyPanelState();
    layoutDialog();
}

void ExportDialog::hide() {
    m_visible = false;
    setVisible(false);
    if (m_exporting.load()) {
        m_cancelRequested.store(true);
        // Non-blocking: destructor will join the future when needed
    } else {
        restoreAudioStreamIfNeeded();
    }
}

// What each state shows. Controls that do not belong to the state are hidden, so the
// layout below only places what is visible and no hidden widget can take a click.
void ExportDialog::applyPanelState() {
    const bool settings = m_panelState == PanelState::Settings;
    const bool progress = m_panelState == PanelState::Progress;
    const bool complete = m_panelState == PanelState::Complete;
    for (const auto& c : {m_outputLabel, m_scopeLabel, m_rateLabel, m_depthLabel, m_tailLabel})
        c->setVisible(settings);
    for (const auto& c : {m_outputInput, m_tailField})
        c->setVisible(settings);
    for (const auto& c : {m_scopeDropdown, m_rateDropdown, m_depthDropdown})
        c->setVisible(settings);
    m_browseButton->setVisible(settings);
    m_stemsCheckbox->setVisible(settings);
    m_stemsHint->setVisible(settings && m_stems);
    m_exportButton->setVisible(settings);
    m_cancelButton->setVisible(settings || progress);
    m_doneButton->setVisible(complete);
    m_statusLabel->setVisible(progress || complete);
    m_detailLabel->setVisible(complete);
    m_detailLabel2->setVisible(complete && m_exportError.empty());
    m_progressBar->setVisible(progress);
    m_elapsedLabel->setVisible(progress);

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    if (progress) {
        m_statusLabel->setText(m_stems ? "Rendering stems..." : "Rendering audio...");
        m_statusLabel->setTextColor(theme.getColor("textPrimary"));
        m_cancelButton->setText("Cancel");
    } else if (settings) {
        m_cancelButton->setText("Cancel");
        if (m_stems) {
            const std::filesystem::path base(m_outputPath);
            m_stemsHint->setText("Written to " + shortenPath(base.parent_path().string(), 40) + " as \"" +
                                 base.stem().string() + " - 01 Track.wav\", one per track with sound.");
        }
    } else if (complete) {
        const bool ok = m_exportError.empty();
        m_statusLabel->setText(ok ? "Export Complete" : "Export Failed");
        m_statusLabel->setTextColor(theme.getColor(ok ? "success" : "error"));
        if (ok) {
            m_detailLabel->setText(m_exportFileCount > 1
                                       ? std::to_string(m_exportFileCount) + " files in " +
                                             shortenPath(m_exportResultPath, 46)
                                       : shortenPath(m_exportResultPath, 50));
            char stats[128];
            std::snprintf(stats, sizeof(stats), "Duration: %.1fs  |  Peak: %.1f dB", m_exportDuration, m_exportPeakDb);
            m_detailLabel2->setText(stats);
        } else {
            m_detailLabel->setText(m_exportError);
        }
        m_doneButton->setText(ok ? "Done" : "Close");
    }
    setDirty(true);
}

void ExportDialog::layoutDialog() {
    namespace L = AestraUI::Layout;
    const auto parentBounds = getBounds();
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();
    m_dialogRect = NUIRect(parentBounds.x + (parentBounds.width - DIALOG_WIDTH) * 0.5f,
                           parentBounds.y + (parentBounds.height - DIALOG_HEIGHT) * 0.5f, DIALOG_WIDTH,
                           DIALOG_HEIGHT);

    const float pad = theme.spacingL;
    const float rowH = theme.layout.standardControlHeight;
    const float gap = theme.spacingS;
    const float labelW = 110.0f;
    const float btnW = 110.0f;
    const float btnH = theme.layout.dialogActionHeight;

    // Title bar, then the content column, then the button strip.
    NUILocalRect column(m_dialogRect.x + pad, m_dialogRect.y + 56.0f, m_dialogRect.width - pad * 2.0f,
                        m_dialogRect.height - 56.0f - btnH - theme.spacingM * 2.0f);
    const auto nextRow = [&](float height) {
        const auto split = L::splitVertical(column, height);
        column = L::splitVertical(split.trailing, gap).trailing;
        return split.leading;
    };
    const auto place = [&](const std::shared_ptr<AestraUI::NUILabel>& label, const auto& control, float controlW) {
        const NUILocalRect row = nextRow(rowH);
        const auto parts = L::splitHorizontal(row, labelW);
        label->setBounds(toRect(parts.leading));
        control->setBounds(NUIRect(parts.trailing.x, parts.trailing.y,
                                   controlW > 0.0f ? controlW : parts.trailing.width, rowH));
    };

    if (m_panelState == PanelState::Settings) {
        const NUILocalRect outputRow = nextRow(rowH);
        const auto labelSplit = L::splitHorizontal(outputRow, labelW);
        const auto browseSplit = L::splitHorizontal(labelSplit.trailing, labelSplit.trailing.width - 82.0f);
        m_outputLabel->setBounds(toRect(labelSplit.leading));
        m_outputInput->setBounds(NUIRect(browseSplit.leading.x, browseSplit.leading.y, browseSplit.leading.width - 10.0f, rowH));
        m_browseButton->setBounds(toRect(browseSplit.trailing));
        place(m_scopeLabel, m_scopeDropdown, 0.0f);
        place(m_rateLabel, m_rateDropdown, 0.0f);
        place(m_depthLabel, m_depthDropdown, 0.0f);
        place(m_tailLabel, m_tailField, 80.0f);
        const NUILocalRect stemsRow = nextRow(rowH);
        m_stemsCheckbox->setBounds(toRect(stemsRow));
        if (m_stems) {
            m_stemsHint->setBounds(toRect(nextRow(rowH * 1.4f)));
        }
    } else if (m_panelState == PanelState::Progress) {
        m_statusLabel->setBounds(toRect(nextRow(rowH)));
        m_progressBar->setBounds(toRect(nextRow(20.0f)));
        m_elapsedLabel->setBounds(toRect(nextRow(rowH)));
    } else {
        m_statusLabel->setBounds(toRect(nextRow(rowH)));
        m_detailLabel->setBounds(toRect(nextRow(rowH * 2.0f)));
        m_detailLabel2->setBounds(toRect(nextRow(rowH)));
    }

    // Buttons, centred at the bottom.
    const float btnY = m_dialogRect.bottom() - btnH - theme.spacingM;
    if (m_panelState == PanelState::Settings) {
        const float total = btnW * 2.0f + theme.spacingS;
        const float x0 = m_dialogRect.x + (m_dialogRect.width - total) * 0.5f;
        m_exportButton->setBounds(NUIRect(x0, btnY, btnW, btnH));
        m_cancelButton->setBounds(NUIRect(x0 + btnW + theme.spacingS, btnY, btnW, btnH));
    } else {
        const NUIRect centred(m_dialogRect.x + (m_dialogRect.width - btnW) * 0.5f, btnY, btnW, btnH);
        m_cancelButton->setBounds(centred);
        m_doneButton->setBounds(centred);
    }
}

void ExportDialog::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (m_panelState == PanelState::Progress && m_exporting) {
        m_exportElapsed += deltaTime;
        m_progressBar->setProgress(std::clamp(static_cast<double>(m_progress.load()), 0.0, 1.0));
        char timeBuf[32];
        std::snprintf(timeBuf, sizeof(timeBuf), "Elapsed: %d:%02d", static_cast<int>(m_exportElapsed) / 60,
                      static_cast<int>(m_exportElapsed) % 60);
        m_elapsedLabel->setText(timeBuf);
    }

    if (m_exportFuture.valid() &&
        m_exportFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        applyExportResult(m_exportFuture.get());
    }

    if (auto pickedPath = m_fileDialog.takeResult()) {
        if (!pickedPath->empty()) {
            m_outputPath = *pickedPath;
            m_outputInput->setText(m_outputPath);
            applyPanelState();
        }
        m_browseButton->setText("Browse");
        m_browseButton->setEnabled(true);
        setDirty(true);
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
        m_exportFileCount = result.fileCount;
        m_exportDuration = result.durationSeconds;
        m_exportPeakDb = result.peakDb;
        m_exportError.clear();
    } else {
        m_exportResultPath.clear();
        m_exportDuration = 0.0;
        m_exportPeakDb = -96.0;
        m_exportError = result.errorMessage;
    }

    applyPanelState();
    layoutDialog();
}

void ExportDialog::failToComplete(const std::string& message) {
    m_panelState = PanelState::Complete;
    m_exportError = message;
    m_exportResultPath.clear();
    m_exportDuration = 0.0;
    m_exportPeakDb = -96.0;
    applyPanelState();
    layoutDialog();
}

void ExportDialog::finish() {
    hide();
    if (m_onExportComplete)
        m_onExportComplete(m_exportError.empty(), m_exportResultPath, m_exportDuration, m_exportPeakDb);
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
        bool ok = deviceManager->startStream();
        if (ok) {
            m_resumeAudioStreamAfterExport = false;
        } else {
            Aestra::Log::warning("[ExportDialog] Failed to restart audio stream after export; will retry.");
        }
    }
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();

    renderer.fillRect(getBounds(), theme.getColor("overlay"));
    renderer.drawShadow(m_dialogRect, 0.0f, props.spacingS, props.spacingL, theme.getColor("shadow"));

    // Surface and title bar come from the shared dialog chrome (spec 2 §5).
    AestraUI::DialogChrome chrome;
    chrome.panel = m_dialogRect;
    chrome.title = (m_panelState == PanelState::Settings)   ? "Export Audio"
                   : (m_panelState == PanelState::Progress) ? "Rendering..."
                                                            : "Export Complete";
    chrome.showClose = false; // The panel's own Cancel/Close button is the dismissal.
    AestraUI::drawDialogChrome(renderer, chrome);

    renderChildren(renderer);
    // An open dropdown's list paints over the controls below it.
    for (const auto& dropdown : {m_scopeDropdown, m_rateDropdown, m_depthDropdown}) {
        if (dropdown->isVisible() && dropdown->isOpen())
            dropdown->renderDropdownList(renderer);
    }
}

bool ExportDialog::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!m_visible) return false;
    NUIComponent::onMouseEvent(event);
    return true; // Modal
}

bool ExportDialog::onKeyEvent(const AestraUI::NUIKeyEvent& event) {
    if (!m_visible) return false;
    if (!event.pressed) return true;

    const bool typing = m_panelState == PanelState::Settings && (m_outputInput->isFocused() || m_tailField->isFocused());
    if (typing) {
        if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
            m_outputInput->setFocused(false);
            m_tailField->setFocused(false);
            syncTailInputFromValue();
            m_tailField->setText(m_tailInput);
            return true;
        }
        m_outputInput->onKeyEvent(event);
        m_tailField->onKeyEvent(event);
        return true;
    }

    if (event.keyCode == AestraUI::NUIKeyCode::Escape) {
        if (m_panelState == PanelState::Progress && m_exporting) {
            m_cancelRequested = true;
        } else if (m_panelState == PanelState::Complete) {
            finish();
        } else {
            hide();
        }
        return true;
    }
    return true; // Modal
}

void ExportDialog::openBrowse() {
    if (m_fileDialog.isPending())
        return;
    Aestra::IPlatformUtils::SaveFileDialogOptions options;
    options.title = "Export Audio";
    options.filter = std::string("WAV Files\0*.wav\0All Files\0*.*\0", sizeof("WAV Files\0*.wav\0All Files\0*.*\0") - 1);
    options.defaultPath = m_outputPath;
    options.defaultExtension = "wav";
    // Result arrives in onUpdate(); the UI keeps painting meanwhile.
    m_fileDialog.start([options = std::move(options)](Aestra::IPlatformUtils& utils) {
        return utils.saveFileDialog(options);
    });
    m_browseButton->setText("Waiting...");
    m_browseButton->setEnabled(false);
    setDirty(true);
}

void ExportDialog::startExport() {
    if (!m_engine || !m_trackManager) return;

    if (m_exportFuture.valid() &&
        m_exportFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        m_exportError = "An export is already in progress.";
        Aestra::Log::warning("[ExportDialog] Export already in progress, ignoring request.");
        return;
    }

    m_tailInput = m_tailField->getText();
    m_outputPath = m_outputInput->getText();

    double parsedTail = 0.0;
    if (!parseTailInput(parsedTail)) {
        failToComplete("Invalid tail value");
        return;
    }
    if (m_outputPath.empty()) {
        failToComplete("No output path specified");
        return;
    }

    m_tailSeconds = parsedTail;

    // V8-C14 step 5d: remember what this export actually ran with. One write
    // per export (the store saves write-through) at the commit point rather
    // than on every dropdown change, so a dialog the user opens and abandons
    // leaves the stored preference alone.
    if (auto* store = Aestra::ServiceLocator::get<Aestra::UISurfaceStoreFile>()) {
        Aestra::ExportDialogValues used;
        used.sampleRateIndex = m_selectedSampleRate;
        used.bitDepthIndex = m_selectedBitDepth;
        used.scopeIndex = m_selectedScope;
        used.tailInput = m_tailInput;
        used.outputDirectory = std::filesystem::path(m_outputPath).parent_path().string();
        store->setDialogExportOptions(Aestra::captureExportOptions(used));
    }

    m_panelState = PanelState::Progress;
    m_progress = 0.0f;
    m_exporting = true;
    m_cancelRequested = false;
    m_exportError.clear();
    m_exportElapsed = 0.0f;
    m_progressBar->setProgress(0.0);
    applyPanelState();
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
            m_outputPath, m_selectedBitDepth, m_selectedSampleRate, m_selectedScope, m_tailSeconds, m_stems);
    } catch (const std::exception& e) {
        if (m_resumeAudioStreamAfterExport) {
            if (auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>()) {
                deviceManager->startStream();
            }
            m_resumeAudioStreamAfterExport = false;
        }
        m_exporting = false;
        Aestra::Log::error(std::string("[ExportDialog] Failed to start export worker: ") + e.what());
        failToComplete(std::string("Failed to start export worker: ") + e.what());
    }
}

ExportDialog::ExportJobResult ExportDialog::exportThreadFn(std::string outputPath, int selectedBitDepth, int selectedSampleRate, int selectedScope, double tailSeconds, bool stems) {
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

    if (stems) {
        // The chosen path names the set: its folder is where the files go, its name is their prefix.
        const std::filesystem::path chosen(config.outputPath);
        const auto result = Aestra::Audio::exportStems(
            *m_engine, *m_trackManager, config, chosen.parent_path().string(), chosen.stem().string(),
            /*includeMaster=*/true, [this](float pct) { m_progress.store(pct, std::memory_order_relaxed); },
            [this]() { return m_cancelRequested.load(std::memory_order_relaxed); });
        ExportJobResult job;
        job.success = result.ok;
        job.errorMessage = result.error;
        job.outputPath = chosen.parent_path().string();
        job.fileCount = static_cast<int>(result.files.size());
        for (const auto& file : result.files) {
            job.durationSeconds = std::max(job.durationSeconds, file.result.durationSeconds);
            job.peakDb = std::max(job.peakDb, file.result.peakDb);
        }
        return job;
    }

    Aestra::Audio::AudioExporter exporter(*m_engine, *m_trackManager);
    exporter.setProgressCallback([this](float pct) {
        m_progress.store(pct, std::memory_order_relaxed);
    });
    exporter.setCancelCheck([this]() {
        return m_cancelRequested.load(std::memory_order_relaxed);
    });

    auto result = exporter.render(config);
    ExportJobResult job;
    job.success = result.success;
    job.outputPath = result.outputPath;
    job.durationSeconds = result.durationSeconds;
    job.peakDb = result.peakDb;
    job.errorMessage = result.errorMessage;
    return job;
}
