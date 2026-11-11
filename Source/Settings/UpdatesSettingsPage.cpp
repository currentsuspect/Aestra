// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdatesSettingsPage.h"

#include "../App/UpdateNotifier.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "UpdateSettings.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace Aestra {

using namespace License;

namespace {

constexpr double kCheckGiveUpSeconds = 30.0;

// Greedy word wrap to a pixel width, honouring existing line breaks, capped so a long note cannot run off the page.
std::vector<std::string> wrapText(AestraUI::NUIRenderer& renderer, const std::string& text, float fontSize, float width,
                                  size_t maxLines) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph) && lines.size() < maxLines) {
        std::istringstream words(paragraph);
        std::string word, line;
        while (words >> word) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && renderer.measureText(candidate, fontSize).width > width) {
                lines.push_back(line);
                line = word;
                if (lines.size() >= maxLines) break;
            } else {
                line = candidate;
            }
        }
        if (!line.empty() && lines.size() < maxLines) lines.push_back(line);
        if (paragraph.empty() && lines.size() < maxLines) lines.push_back("");
    }
    return lines;
}

} // namespace

UpdatesSettingsPage::UpdatesSettingsPage(std::shared_ptr<UpdateNotifier> notifier) : m_notifier(std::move(notifier)) {
    createUI();
    syncFromSettings();
}

void UpdatesSettingsPage::createUI() {
    m_checkButton = std::make_shared<AestraUI::NUIButton>();
    m_checkButton->setText("Check now");
    m_checkButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_checkButton->setOnClick([this]() { checkNow(); });
    addChild(m_checkButton);

    m_installButton = std::make_shared<AestraUI::NUIButton>();
    m_installButton->setStyle(AestraUI::NUIButton::Style::Primary);
    m_installButton->setOnClick([this]() { onInstallClicked(); });
    m_installButton->setVisible(false);
    addChild(m_installButton);

    m_cancelButton = std::make_shared<AestraUI::NUIButton>();
    m_cancelButton->setText("Cancel");
    m_cancelButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_cancelButton->setOnClick([this]() {
        if (m_notifier) m_notifier->cancelInstall();
    });
    m_cancelButton->setVisible(false);
    addChild(m_cancelButton);

    m_stableButton = std::make_shared<AestraUI::NUIButton>();
    m_stableButton->setText("Stable");
    m_stableButton->setOnClick([this]() { chooseChannel("stable"); });
    addChild(m_stableButton);

    m_earlyButton = std::make_shared<AestraUI::NUIButton>();
    m_earlyButton->setText("Early access");
    m_earlyButton->setOnClick([this]() { chooseChannel("early"); });
    addChild(m_earlyButton);

    m_keyInput = std::make_shared<AestraUI::NUITextInput>();
    m_keyInput->setInputType(AestraUI::NUITextInput::InputType::Password); // a key is not for shoulder surfers
    m_keyInput->setPlaceholderText("Early access key");
    m_keyInput->setMaxLength(128);
    m_keyInput->setOnReturnKey([this]() { commitKey(true); });
    m_keyInput->setOnFocusLost([this]() { commitKey(true); });
    addChild(m_keyInput);

    m_autoToggle = std::make_shared<AestraUI::NUIToggle>();
    m_autoToggle->setAnimated(false);
    m_autoToggle->setOnToggle([this](bool on) {
        if (!m_notifier) return;
        UpdateSettings s = m_notifier->settings();
        s.autoCheck = on;
        m_notifier->setSettings(s);
    });
    addChild(m_autoToggle);
}

void UpdatesSettingsPage::syncFromSettings() {
    if (!m_notifier) return;
    const UpdateSettings s = m_notifier->settings();
    m_early = s.channel == "early";
    m_stableButton->setStyle(m_early ? AestraUI::NUIButton::Style::Secondary : AestraUI::NUIButton::Style::Primary);
    m_earlyButton->setStyle(m_early ? AestraUI::NUIButton::Style::Primary : AestraUI::NUIButton::Style::Secondary);
    m_keyInput->setVisible(m_early);
    if (m_keyInput->getText() != s.founderKey && !m_keyInput->isFocused()) m_keyInput->setText(s.founderKey);
    m_autoToggle->setOn(s.autoCheck);
    layoutComponents();
}

void UpdatesSettingsPage::onShow() {
    syncFromSettings();
    repaint();
}

void UpdatesSettingsPage::chooseChannel(const std::string& channel) {
    if (!m_notifier) return;
    UpdateSettings s = m_notifier->settings();
    if (s.channel == channel) return;
    s.channel = channel;
    s.lastNotified.clear(); // a different channel's newest build is news again
    m_notifier->setSettings(s);
    m_ignoreResultsBefore = m_notifier->resultCount(); // what was found on the other channel says nothing about this one
    syncFromSettings();
    if (channel == "stable" || !s.founderKey.empty()) checkNow(); // look straight away, so the status is about this channel
    repaint();
}

void UpdatesSettingsPage::commitKey(bool thenCheck) {
    if (!m_notifier) return;
    std::string key = m_keyInput->getText();
    // A pasted key often carries a space or a newline from where it was copied.
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c) != 0; }), key.end());
    UpdateSettings s = m_notifier->settings();
    if (s.founderKey == key) return;
    s.founderKey = key;
    m_notifier->setSettings(s);
    if (m_keyInput->getText() != key) m_keyInput->setText(key);
    if (thenCheck && !key.empty()) checkNow();
    repaint();
}

void UpdatesSettingsPage::checkNow() {
    if (!m_notifier) return;
    commitKey(false); // a key typed but not yet confirmed counts for this check
    m_resultCountAtCheck = m_notifier->resultCount();
    m_checking = true;
    m_checkingFor = 0.0;
    m_notifier->checkNow();
    repaint();
}

bool UpdatesSettingsPage::installVisible() const {
    if (!m_notifier) return false;
    const InstallPhase phase = m_notifier->installState().phase;
    if (phase != InstallPhase::Idle) return true;
    const bool fresh = m_notifier->hasResult() && m_notifier->resultCount() > m_ignoreResultsBefore;
    return fresh && !m_checking && m_notifier->lastResult().status == UpdateCheckStatus::Available;
}

void UpdatesSettingsPage::onInstallClicked() {
    if (!m_notifier) return;
    const InstallState st = m_notifier->installState();
    if (st.phase == InstallPhase::Ready) m_notifier->restartToUpdate();
    else m_notifier->startInstall(); // Idle, Failed ("Try again") and Cancelled all begin (or resume) a download
    repaint();
}

void UpdatesSettingsPage::onUpdate(double deltaTime) {
    AestraUI::NUIComponent::onUpdate(deltaTime);
    if (!m_notifier) return;
    // The install runs on its own thread: follow it, and re-lay the page when its card appears or goes.
    const InstallPhase phase = m_notifier->installState().phase;
    if (phase == InstallPhase::Downloading || phase == InstallPhase::Unpacking) repaint();
    if (installVisible() != m_installShown) {
        layoutComponents();
        repaint();
    }
    if (!m_checking) return;
    m_checkingFor += deltaTime;
    if (m_notifier->resultCount() != m_resultCountAtCheck || m_checkingFor > kCheckGiveUpSeconds) {
        m_checking = false;
        repaint();
    }
}

void UpdatesSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

// Board 2's form: the status (with Check now) and an install in progress are blocks this page draws; channel,
// key and the automatic check are ordinary rows.
void UpdatesSettingsPage::layoutComponents() {
    const bool fresh = m_notifier && m_notifier->hasResult() && m_notifier->resultCount() > m_ignoreResultsBefore;
    const bool notes = fresh && !m_checking && m_notifier->lastResult().status == UpdateCheckStatus::Available;
    m_installShown = installVisible();
    m_installButton->setVisible(m_installShown);
    if (!m_installShown)
        m_cancelButton->setVisible(false);

    m_form.clear();
    m_form.section("THIS COPY");
    m_statusItem = m_form.block(nullptr, 64.0f);
    m_installItem = m_installShown ? m_form.block(nullptr, 72.0f) : SIZE_MAX;
    m_form.section("CHANNEL");
    m_form.row("Builds", m_stableButton,
               m_early ? "Early builds, before they reach everyone" : "Tested builds for everyone", 96.0f);
    if (m_early) {
        m_form.row("Early access key", m_keyInput, "", 300.0f);
    }
    m_keyInput->setVisible(m_early);
    m_form.section("AUTOMATIC");
    m_form.row("Check for updates", m_autoToggle, "after Aestra starts, then daily; nothing installs by itself", 40.0f);
    m_notesItem = notes ? m_form.block(nullptr, 150.0f) : SIZE_MAX;
    m_form.layout(getBounds());

    // Early access sits beside Stable, as one choice; the hint moves past both.
    const auto& stable = m_stableButton->getBounds();
    m_earlyButton->setBounds(AestraUI::NUIRect(stable.right() + 6.0f, stable.y, 112.0f, stable.height));
    m_earlyButton->setVisible(m_stableButton->isVisible());
    m_form.setHint(m_stableButton.get(), "");
    m_channelHint = m_early ? "Early builds, before they reach everyone" : "Tested builds for everyone";
    m_channelHintX = m_earlyButton->getBounds().right() + 12.0f;

    const auto status = m_form.boundsOf(m_statusItem);
    m_checkButton->setBounds(
        AestraUI::NUIRect(status.right() - 110.0f, status.y + (status.height - 28.0f) * 0.5f, 110.0f, 28.0f));
    if (m_installShown) {
        const auto install = m_form.boundsOf(m_installItem);
        m_installButton->setBounds(AestraUI::NUIRect(install.right() - 170.0f, install.y + 12.0f, 170.0f, 28.0f));
        m_cancelButton->setBounds(AestraUI::NUIRect(install.right() - 254.0f, install.y + 12.0f, 76.0f, 28.0f));
    }
}

void UpdatesSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    if (getBounds().isEmpty() || !m_notifier) return;
    const auto textPrimary = theme.getColor("textPrimary");
    const auto textSecondary = theme.getColor("textSecondary");
    const auto textMuted = theme.getColor("textMuted");
    const float title = 14.0f;
    const float normal = 13.0f;
    const float small = 11.5f;
    const UpdateSettings settings = m_notifier->settings();
    const UpdateCheckResult result = m_notifier->lastResult();
    const std::string current = UpdateNotifier::currentVersion();
    // A result only speaks for the channel it was found on: after a switch, wait for a fresh one. Early access with no key
    // has nothing to ask, so it says what is missing instead of showing an old answer.
    const bool fresh = m_notifier->hasResult() && m_notifier->resultCount() > m_ignoreResultsBefore;
    UpdateCheckResult shown = result;
    if (settings.channel == "early" && settings.founderKey.empty()) shown.status = UpdateCheckStatus::Unauthorized;
    const bool haveAnswer = fresh || (settings.channel == "early" && settings.founderKey.empty());
    const UpdateStatusText status = describeUpdateStatus(m_checking, haveAnswer, shown, settings, current);

    m_form.paint(renderer);
    renderer.setClipRect(m_form.area());

    // Status
    const auto statusRect = m_form.boundsOf(m_statusItem);
    const auto tone = status.tone == UpdateStatusText::Tone::Good   ? theme.getColor("success")
                      : status.tone == UpdateStatusText::Tone::Warn ? theme.getColor("warning")
                                                                    : textPrimary;
    renderer.drawText(status.headline, {statusRect.x, statusRect.y + 12.0f}, title, tone);
    if (!status.detail.empty()) {
        const float room = m_checkButton->getBounds().x - statusRect.x - 14.0f;
        const auto lines = wrapText(renderer, status.detail, small, room, 2);
        for (size_t i = 0; i < lines.size(); ++i)
            renderer.drawText(lines[i], {statusRect.x, statusRect.y + 34.0f + static_cast<float>(i) * (small + 4.0f)},
                              small, textMuted);
    }

    // Install
    if (m_installShown) {
        const auto installRect = m_form.boundsOf(m_installItem);
        const InstallState st = m_notifier->installState();
        const float left = installRect.x;
        const float room = m_cancelButton->getBounds().x - left - 14.0f;
        std::string headline, detail;
        const bool busy = st.phase == InstallPhase::Downloading || st.phase == InstallPhase::Unpacking;
        switch (st.phase) {
        case InstallPhase::Downloading:
            headline = "Downloading " + st.version;
            detail = updateSizeText(st.done) + " of " + updateSizeText(st.total);
            m_installButton->setText("Downloading...");
            break;
        case InstallPhase::Unpacking:
            headline = "Getting " + st.version + " ready";
            detail = "Checking and unpacking the download.";
            m_installButton->setText("Please wait...");
            break;
        case InstallPhase::Ready:
            headline = st.version + " is ready";
            detail = "Aestra will ask about unsaved work, close, update and open again.";
            m_installButton->setText("Restart and update");
            break;
        case InstallPhase::Failed:
            headline = "The update did not install";
            detail = st.message;
            m_installButton->setText("Try again");
            break;
        case InstallPhase::Cancelled:
            headline = "Download cancelled";
            detail = "What was downloaded is kept, so it carries on from there.";
            m_installButton->setText("Download and install");
            break;
        case InstallPhase::Idle:
        default:
            headline = "Install " + result.manifest.version;
            detail = "Downloads " + updateSizeText(result.manifest.size) + ", checks it, then asks you to restart.";
            m_installButton->setText("Download and install");
            break;
        }
        m_installButton->setEnabled(!busy);
        m_cancelButton->setVisible(st.phase == InstallPhase::Downloading);
        renderer.drawText(headline, {left, installRect.y + 12.0f}, normal, textPrimary);
        const auto lines = wrapText(renderer, detail, small, room, 2);
        for (size_t i = 0; i < lines.size(); ++i)
            renderer.drawText(lines[i], {left, installRect.y + 32.0f + static_cast<float>(i) * (small + 3.0f)}, small,
                              st.phase == InstallPhase::Failed ? theme.getColor("warning") : textMuted);
        if (busy) {
            const AestraUI::NUIRect bar(left, installRect.bottom() - 10.0f, installRect.width, 3.0f);
            renderer.fillRect(bar, theme.getColor("border"));
            const float fraction = st.phase == InstallPhase::Unpacking ? 1.0f : (st.total > 0 ? std::min(1.0f, static_cast<float>(static_cast<double>(st.done) / static_cast<double>(st.total))) : 0.0f);
            renderer.fillRect(AestraUI::NUIRect(bar.x, bar.y, std::max(3.0f, bar.width * fraction), bar.height),
                              theme.getColor("primary"));
        }
    }

    // Stable and Early access are one choice (the chosen one is filled); its hint follows both.
    if (m_stableButton->isVisible()) {
        renderer.drawText(m_channelHint, {m_channelHintX, renderer.calculateTextY(m_stableButton->getBounds(), small)},
                          small, textMuted);
    }

    // What's new, when there is something
    if (m_notesItem != SIZE_MAX) {
        const auto notesRect = m_form.boundsOf(m_notesItem);
        renderer.drawText("WHAT'S NEW IN " + result.manifest.version, {notesRect.x, notesRect.y + 12.0f},
                          theme.getCurrentTheme().fontSizeXS, textMuted);
        const float lineH = normal + 5.0f;
        const size_t fit = static_cast<size_t>(std::max(1.0f, (notesRect.height - 40.0f) / lineH));
        const auto lines =
            wrapText(renderer, result.manifest.notes.empty() ? "No notes for this build." : result.manifest.notes,
                     normal, notesRect.width, fit);
        for (size_t i = 0; i < lines.size(); ++i)
            renderer.drawText(lines[i], {notesRect.x, notesRect.y + 32.0f + static_cast<float>(i) * lineH}, normal,
                              textSecondary);
    }

    renderChildren(renderer);
    renderer.clearClipRect();
}

} // namespace Aestra
