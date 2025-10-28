#pragma once

#include "../NomadAudio/include/TrackManager.h"
#include "TrackUIComponent.h"
#include "../NomadUI/Core/NUIComponent.h"
#include <memory>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief UI wrapper for TrackManager
 *
 * Provides visual track management interface with:
 * - Track layout and scrolling
 * - Add/remove track functionality
 * - Visual timeline integration
 */
class TrackManagerUI : public NomadUI::NUIComponent {
public:
    TrackManagerUI(std::shared_ptr<TrackManager> trackManager);
    ~TrackManagerUI() override;

    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager; }

    // Track Management
    void addTrack(const std::string& name = "");
    void refreshTracks();

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    
    // 🔥 VIEWPORT CULLING: Override to only render visible tracks
    void renderChildren(NomadUI::NUIRenderer& renderer);

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<TrackUIComponent>> m_trackUIComponents;

    // UI Layout
    int m_trackHeight{80};
    int m_trackSpacing{5};

    // Track management UI
    std::shared_ptr<NomadUI::NUIButton> m_addTrackButton;

    void layoutTracks();
    void onAddTrackClicked();
    void updateTrackPositions();
};

} // namespace Audio
} // namespace Nomad
