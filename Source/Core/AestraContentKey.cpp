// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The transport bar's KEY module (V83-KEY3, FD-98): the song key as one undoable command per change, and the
// computer keyboard's root. Undo, redo and Muse change the key behind the bar's back, so it is re-read each frame.
#include "AestraContent.h"
#include "Commands/SetProjectKeyCommand.h"
#include "TransportBar.h"

void AestraContent::wireKeyModule() {
    m_transportBar->setOnTypingChange([this](bool enabled, int octave) {
        m_musicalTyping.setEnabled(enabled);
        m_musicalTyping.setDisplayOctave(octave);
        m_transportBar->setMusicalTypingStatus(m_musicalTyping.isEnabled(), m_musicalTyping.displayOctave());
    });
    m_transportBar->setOnKeyChange([this](const Aestra::Audio::ProjectHarmony& next, const std::string& name) {
        if (!m_trackManager) return;
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<Aestra::Audio::SetProjectKeyCommand>(*m_trackManager, next, name));
    });
}

void AestraContent::showKeyModule() {
    if (m_transportBar && m_trackManager) m_transportBar->showKey(m_trackManager->getUnitManager().harmony());
}
