
/**
 * @brief Handle Play button click.
 */
void ArsenalPanel::onPlayClicked() {
    if (!m_trackManager || !m_activePatternID.isValid()) {
        Aestra::Log::warn("[Arsenal] No active pattern to play");
        return;
    }
    
    m_isPlaying = true;
    m_trackManager->playPatternInArsenal(m_activePatternID);
    
    Aestra::Log::info("[Arsenal] Playing pattern " + std::to_string(m_activePatternID.value));
}

/**
 * @brief Handle Stop button click.
 */
void ArsenalPanel::onStopClicked() {
    if (!m_trackManager) return;
    
    m_isPlaying = false;
    m_trackManager->stopArsenalPlayback();
    
    Aestra::Log::info("[Arsenal] Stopped playback");
}
