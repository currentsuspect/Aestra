// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The "no sound yet" toast (Arsenal coherence, mock q2 option C). The rules live in Panels/ArsenalEmptyNotice.h; this
// file only connects them to the Arsenal panel, the transport and the shared toast.
#include "AestraContent.h"

#include "../Panels/ArsenalPanel.h"
#include "TrackManager.h"

using Aestra::Audio::UnitID;

void AestraContent::wireEmptySoundNotice() {
    if (!m_sequencerPanel) return;
    // A pad pressed and released on a row: tell the user if that unit is silent.
    m_sequencerPanel->setOnStepGestureEnd([this](UnitID id) {
        if (m_sequencerPanel && m_sequencerPanel->isUnitEmpty(id)) noteEmptySounds({id});
    });
}

void AestraContent::noteEmptySounds(const std::vector<UnitID>& units) {
    for (const UnitID id : units) m_emptyNotice.note(id);
}

void AestraContent::updateEmptySoundNotice() {
    if (!m_notificationToast || !m_sequencerPanel) return;

    // Keep the list clear of the toast while it shows: the list's bottom edge rises above it.
    m_sequencerPanel->setKeepClear(m_notificationToast->actionShowing() && m_sequencerPanel->isVisible()
                                       ? m_notificationToast->actionRect()
                                       : AestraUI::NUIRect{});

    if (!m_emptyNotice.hasPending() || !m_trackManager || !m_sequencerPanel->isVisible() || m_viewFocus != ViewFocus::Arsenal) return;
    const bool recording = m_trackManager->isRecording() || m_trackManager->isRecordArmed();
    const auto ids = m_emptyNotice.take(recording, [this](UnitID id) { return m_sequencerPanel->isUnitEmpty(id); });
    if (ids.empty()) return;

    auto& units = m_trackManager->getUnitManager();
    std::vector<std::string> names;
    uint32_t color = 0x808080;
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto* unit = units.getUnit(ids[i]);
        if (!unit) continue;
        names.push_back(unit->name);
        if (names.size() == 1) color = unit->color & 0xFFFFFFu; // the first unit's own colour, never the accent
    }
    if (names.empty()) return;

    const UnitID first = ids.front();
    AestraUI::ActionToastSpec spec;
    spec.text = Aestra::ArsenalEmptyNotice::message(names);
    spec.dotRgb = color;
    spec.primaryLabel = "Load a sound";
    spec.secondaryLabel = "Instrument or plugin";
    spec.onPrimary = [this, first]() { if (m_sequencerPanel) m_sequencerPanel->requestLoadSample(first); };
    // The row has no instrument/plugin menu of its own; the Arsenal's type list (under "+ Add a sound") is the one choice offered.
    spec.onSecondary = [this]() { if (m_sequencerPanel) m_sequencerPanel->openSoundTypePicker(); };
    showActionToast(std::move(spec));
}
