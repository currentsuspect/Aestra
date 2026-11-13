// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Rumble's screen (FD-80, SPEC-025 R5): find a sound by picking, then shape it.
//
//   Start from  |  trail of steps
//   (shelf)     |  the parent, a hairline, six sounds made from it
//               |  More like this · Close / Further / Far · Hold chips · Back
//               |  the picture: outline, pitch line, length square, pitch-drop dot
//               |  six controls: Punch, Tail, Weight, Tone, Dirt, Slide
//               |  one sentence saying what the last move changed
//
// The moves live in RumbleFindSession (headless, tested). This file draws it and hands the picked sound to the
// instrument after every move. Outlines come from RumbleOutlineWorker, never from the UI thread.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include "RumbleKept.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Plugins {
class RumbleFindSession;
class RumbleOutlineWorker;
struct RumbleOutline;
struct RumbleSound;
} // namespace Plugins
} // namespace Aestra

namespace AestraUI {

class RumblePluginEditor : public AestraPanelWindow {
public:
    // One shelf row per starting sound (kRumbleFactoryPresets; checked by a static_assert in the .cpp).
    static constexpr size_t kPresetSlots = 7;
    static constexpr size_t kControlCount = 6;

    explicit RumblePluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~RumblePluginEditor() override;

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    // Left and Right move between the sounds, Down makes six more, Up goes back a step (SPEC-025 R8).
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onResize(int width, int height) override;

    // How an audition reaches the audio engine: the app plays @p key on the unit that owns @p instance, as the
    // Sample Editor's Preview does. Set once at start-up (AestraContent); unset in tests.
    using AuditionSink = std::function<void(const Aestra::Audio::IPluginInstance* instance, int key)>;
    static void setAuditionSink(AuditionSink sink);

    // For tests and the screenshot recipe: the session the screen draws.
    const Aestra::Plugins::RumbleFindSession& session() const { return *m_session; }

private:
    enum class Handle { None, Length, Drop };

    void layout();
    void startFrom(size_t presetIndex);
    // Start again from whatever the instrument plays now (opening, or a preset load, Undo or A/B elsewhere).
    void startFromInstrument();
    // When the instrument's sound changed outside this screen, start from it, so a later move never writes
    // the old sound back over the change.
    void followInstrument();
    void startFromKept(size_t index);
    void keep();
    void undoKeep();
    void refreshKept();
    void moved() { m_editorSaid.clear(); } // a move replaces what Keep or Undo said
    void soundChanged(); // hand the current sound to the instrument; ask for outlines
    void audition();     // play the current sound once at C3, its home pitch (R6)
    Aestra::Plugins::RumbleSound instrumentSound() const;

    void drawShelf(NUIRenderer& renderer);
    void drawTrail(NUIRenderer& renderer);
    void drawRow(NUIRenderer& renderer);
    void drawCard(NUIRenderer& renderer, const NUIRect& rect, int index);
    void drawActions(NUIRenderer& renderer);
    void drawPicture(NUIRenderer& renderer);
    void drawControls(NUIRenderer& renderer);
    void drawSaid(NUIRenderer& renderer);
    void drawEverything(NUIRenderer& renderer);
    // An outline in @p rect, mirrored about its middle; false while it is still being made.
    bool drawOutline(NUIRenderer& renderer, const NUIRect& rect, const Aestra::Plugins::RumbleSound& sound,
                     const NUIColor& color, bool picture);

    bool handlePress(const NUIPoint& p);
    void dragHandle(const NUIPoint& p);
    float pictureX(double seconds) const;
    float pictureDropY(double semitones) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::unique_ptr<Aestra::Plugins::RumbleFindSession> m_session;
    std::unique_ptr<Aestra::Plugins::RumbleOutlineWorker> m_outlines;
    std::shared_ptr<const Aestra::Plugins::RumbleOutline> m_lastPicture; // shown while a shaped one is made
    size_t m_startPreset = 0;
    int m_startKept = -1; // the kept sound this session started from, or -1

    // Your 808s (R7): the producer's kept sounds, and the last keep so it can be undone.
    std::unique_ptr<Aestra::Plugins::RumbleKept> m_keptStore;
    std::vector<Aestra::Plugins::RumbleKept::Kept> m_kept;
    std::optional<Aestra::Plugins::RumbleKept::Kept> m_lastKept;
    std::string m_editorSaid;

    // Layout.
    NUIRect m_shelf;
    std::array<NUIRect, kPresetSlots> m_shelfRows{};
    NUIRect m_trail;
    std::array<NUIRect, 18> m_trailPills{}; // the steps that fit, and a "..." either side for the hidden ones
    std::array<size_t, 18> m_trailSteps{};  // the step each pill goes to
    size_t m_trailPillCount = 0;
    std::array<NUIRect, 7> m_cards{}; // [0] the parent, then the six
    NUIRect m_more;
    std::array<NUIRect, 3> m_distance{};
    std::array<NUIRect, 5> m_holds{};
    NUIRect m_back;
    NUIRect m_keep;
    NUIRect m_undo;
    std::array<NUIRect, 8> m_keptRows{};
    NUIRect m_picture;
    std::array<NUIRect, kControlCount> m_controls{};
    NUIRect m_said;
    NUIRect m_everythingToggle;
    std::array<NUIRect, 10> m_everything{}; // the nine detail settings, then Velocity
    bool m_showEverything = false;

    // Pointer.
    NUIPoint m_hover;
    Handle m_dragging = Handle::None;
    long long m_lastClickMs = 0;
    NUIPoint m_lastClickPos;

    static constexpr float kWindowWidth = 920.0f;
    static constexpr float kWindowHeight = 530.0f;
};

} // namespace AestraUI
