
void ArsenalPanel::onPlayClicked() {
    if (!m_trackManager || !m_activePatternID.isValid()) {
        Nomad::Log::warn("[Arsenal] No active pattern to play");
        return;
    }
    
    m_isPlaying = true;
    m_trackManager->playPatternInArsenal(m_activePatternID);
    
    Nomad::Log::info("[Arsenal] Playing pattern " + std::to_string(m_activePatternID.value));
}

void ArsenalPanel::onStopClicked() {
    if (!m_trackManager) return;
    
    m_isPlaying = false;
    m_trackManager->stopArsenalPlayback();
    
    Nomad::Log::info("[Arsenal] Stopped playback");
}
