
// Removed documented symbols that were not declared in the class definition to fix Doxygen warnings.
// void ArsenalPanel::onPlayClicked()
// void ArsenalPanel::onStopClicked()

/*
void ArsenalPanel::onPlayClicked() {
    if (!m_trackManager || !m_activePatternID.isValid()) {
        Aestra::Log::warn("[Arsenal] No active pattern to play");
        return;
    }
    
    m_isPlaying = true;
    m_trackManager->playPatternInArsenal(m_activePatternID);
    
    Aestra::Log::info("[Arsenal] Playing pattern " + std::to_string(m_activePatternID.value));
}

void ArsenalPanel::onStopClicked() {
    if (!m_trackManager) return;
    
    m_isPlaying = false;
    m_trackManager->stopArsenalPlayback();
    
    Aestra::Log::info("[Arsenal] Stopped playback");
}
*/
