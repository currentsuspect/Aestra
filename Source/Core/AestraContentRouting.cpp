// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The ways into the routing graph (V81-R1, FD-25 board 6): the mixer inspector's link, a track header's menu and
// the View menu all land here. Kept out of AestraContent.cpp, which is at its size pin.
#include "AestraContent.h"

#include "MixerPanel.h"
#include "MixerViewModel.h"

using namespace Aestra;

void AestraContent::showRoutingGraph(uint32_t channelId) {
    if (channelId != 0 && m_mixerPanel)
        if (auto vm = m_mixerPanel->getViewModel()) vm->setSelectedChannelId(static_cast<int32_t>(channelId));
    if (m_viewFocus == ViewFocus::RoutingMap) return;
    m_previousViewFocus = m_viewFocus;
    setViewFocus(ViewFocus::RoutingMap);
}
