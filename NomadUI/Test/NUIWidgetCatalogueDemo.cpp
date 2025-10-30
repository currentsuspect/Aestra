// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Core/NUITypes.h"
#include "Widgets/NUICoreWidgets.h"
#include "Widgets/NUITransportWidgets.h"
#include "Widgets/NUIMixerWidgets.h"
#include "Widgets/NUIArrangementWidgets.h"
#include "Widgets/NUIVisualWidgets.h"
#include "Widgets/NUIUtilityWidgets.h"
#include "Widgets/NUIThematicWidgets.h"

using namespace NomadUI;

namespace
{
    std::string boolToString(bool value)
    {
        return value ? "true" : "false";
    }

    std::string toggleStateToString(NUIToggle::State state)
    {
        switch (state)
        {
        case NUIToggle::State::On: return "On";
        case NUIToggle::State::Off: return "Off";
        case NUIToggle::State::Disabled: return "Disabled";
        }
        return "Unknown";
    }

    template <typename DemoFunc>
    void runDemo(const std::string& name, DemoFunc&& func, std::vector<std::string>& registry)
    {
        registry.push_back(name);
        std::cout << "\n=== " << name << " ===\n";
        func();
    }
}

int main()
{
    std::vector<std::string> demos;

    std::cout << "NomadUI Widget Catalogue Demos" << std::endl;
    std::cout << "This program instantiates each NomadUI widget and exercises core APIs." << std::endl;

    runDemo("Core::NUIToggle", []()
    {
        NUIToggle toggle;
        toggle.setAnimated(true);
        toggle.setOnToggle([](bool state)
        {
            std::cout << "  toggled callback -> " << (state ? "playing" : "stopped") << std::endl;
        });
        toggle.setOn(true);
        std::cout << "  animated=" << boolToString(toggle.isAnimated()) << std::endl;
        std::cout << "  current state=" << toggleStateToString(toggle.getState()) << std::endl;
        toggle.setState(NUIToggle::State::Disabled);
        std::cout << "  disabled state=" << toggleStateToString(toggle.getState()) << std::endl;
    }, demos);

    runDemo("Core::NUITextField", []()
    {
        NUITextField field;
        field.setText("Nomad");
        field.setPlaceholder("Enter name");
        std::cout << "  text='" << field.getText() << "' placeholder='" << field.getPlaceholder() << "'" << std::endl;
    }, demos);

    runDemo("Core::NUIMeter", []()
    {
        NUIMeter meter;
        meter.setChannelCount(2);
        meter.setHoldEnabled(true);
        meter.setDecayRate(0.75f);
        meter.setLevels(0, 0.8f, 0.6f);
        meter.setLevels(1, 0.7f, 0.5f);
        for (size_t i = 0; i < meter.getChannelCount(); ++i)
        {
            auto levels = meter.getLevels(i);
            std::cout << "  channel " << i << " peak=" << levels.peak << " rms=" << levels.rms << std::endl;
        }
        std::cout << "  hold enabled=" << boolToString(meter.isHoldEnabled()) << " decay=" << meter.getDecayRate() << std::endl;
    }, demos);

    runDemo("Core::NUIScrollView", []()
    {
        NUIScrollView view;
        view.setContentSize({ 2000.0f, 1200.0f });
        view.setDirection(NUIScrollView::Direction::Both);
        view.setScrollOffset({ 150.0f, 90.0f });
        auto offset = view.getScrollOffset();
        std::cout << "  content size=" << view.getContentSize().width << "x" << view.getContentSize().height << std::endl;
        std::cout << "  offset=(" << offset.x << ", " << offset.y << ")" << std::endl;
    }, demos);

    runDemo("Core::NUIPanel", []()
    {
        NUIPanel panel;
        panel.setVariant(NUIPanel::Variant::Elevated);
        panel.setBackgroundColor(NUIColor::fromHex(0x1e1e28));
        panel.setBorderColor(NUIColor::Primary());
        std::cout << "  variant=Elevated bg alpha=" << panel.getBackgroundColor().a
                  << " border alpha=" << panel.getBorderColor().a << std::endl;
    }, demos);

    runDemo("Core::NUIPopupMenu", []()
    {
        NUIPopupMenu menu;
        std::vector<NUIPopupMenuItem> items = {
            { "new", "New Project", true },
            { "open", "Open...", true },
            { "disabled", "Disabled Item", false }
        };
        menu.setItems(items);
        menu.setOnSelect([](const NUIPopupMenuItem& item)
        {
            std::cout << "  selected item -> " << item.id << " : " << item.label << std::endl;
        });
        for (const auto& item : menu.getItems())
        {
            std::cout << "  item '" << item.label << "' enabled=" << boolToString(item.enabled) << std::endl;
        }
    }, demos);

    runDemo("Core::NUITabBar", []()
    {
        NUITabBar tabBar;
        tabBar.addTab({ "arranger", "Arranger", false });
        tabBar.addTab({ "mixer", "Mixer", false });
        tabBar.addTab({ "browser", "Browser", true });
        tabBar.setActiveTab("mixer");
        std::cout << "  active tab=" << tabBar.getActiveTab() << std::endl;
        std::cout << "  total tabs=" << tabBar.getTabs().size() << std::endl;
        tabBar.removeTab("browser");
        std::cout << "  after removal total tabs=" << tabBar.getTabs().size() << std::endl;
    }, demos);

    runDemo("Transport::Play/Record/Stop", []()
    {
        PlayButton play;
        play.setPlaying(true);
        play.setOnToggle([](bool playing)
        {
            std::cout << "  play toggled -> " << (playing ? "play" : "stop") << std::endl;
        });

        StopButton stop;
        stop.setOnStop([]()
        {
            std::cout << "  stop pressed" << std::endl;
        });

        RecordButton record;
        record.setArmed(true);
        record.setOnToggle([](bool armed)
        {
            std::cout << "  record armed -> " << boolToString(armed) << std::endl;
        });

        LoopToggle loop;
        loop.setOnToggle([](bool enabled)
        {
            std::cout << "  loop toggled -> " << boolToString(enabled) << std::endl;
        });
        loop.setOn(true);
        RewindButton rewind;
        rewind.setOnRewind([]() { std::cout << "  rewind" << std::endl; });
        ForwardButton forward;
        forward.setOnForward([]() { std::cout << "  forward" << std::endl; });

        std::cout << "  play playing=" << boolToString(play.isPlaying()) << std::endl;
        std::cout << "  record armed=" << boolToString(record.isArmed()) << std::endl;
        std::cout << "  loop state=" << boolToString(loop.isOn()) << std::endl;
    }, demos);

    runDemo("Transport::Displays", []()
    {
        TempoDisplay tempo;
        tempo.setTempo(128.0);
        tempo.setOnTempoChanged([](double bpm)
        {
            std::cout << "  tempo changed -> " << bpm << std::endl;
        });

        TimeSignatureDisplay signature;
        signature.setNumerator(7);
        signature.setDenominator(8);

        ClockDisplay clock;
        clock.setTimeString("01:02:03.456");

        MasterVU vu;
        vu.setChannelCount(2);
        vu.setLevels(0, 0.9f, 0.7f);
        vu.setLevels(1, 0.85f, 0.65f);

        CPUIndicator cpu;
        cpu.setLoad(0.42f);

        std::cout << "  tempo=" << tempo.getTempo() << " bpm" << std::endl;
        std::cout << "  time signature=" << signature.getNumerator() << "/" << signature.getDenominator() << std::endl;
        std::cout << "  clock=" << clock.getTimeString() << std::endl;
        std::cout << "  cpu load=" << cpu.getLoad() << std::endl;
    }, demos);

    runDemo("Transport::TransportBar", []()
    {
        TransportBar bar;
        bar.getTempoDisplay().setTempo(140.0);
        bar.getTimeSignatureDisplay().setNumerator(3);
        bar.getTimeSignatureDisplay().setDenominator(4);
        bar.getClockDisplay().setTimeString("02:12:45.001");
        bar.getCPUIndicator().setLoad(0.33f);
        bar.getMasterVU().setChannelCount(2);
        bar.getMasterVU().setLevels(0, 0.95f, 0.80f);
        bar.getMasterVU().setLevels(1, 0.90f, 0.75f);
        std::cout << "  tempo display=" << bar.getTempoDisplay().getTempo() << std::endl;
        std::cout << "  clock display=" << bar.getClockDisplay().getTimeString() << std::endl;
    }, demos);

    runDemo("Mixer::Channel Controls", []()
    {
        ChannelStrip strip;
        strip.getTrackLabel().setText("Lead Synth");
        strip.getTrackLabel().setColor(NUIColor::fromHex(0xf97316));
        strip.getFader().setValue(0.78f);
        strip.getPanKnob().setValue(0.6f);
        strip.getMuteButton().setOn(true);
        strip.getSoloButton().setOn(false);
        strip.getArmButton().setOn(true);
        strip.getMeterStrip().setChannelCount(2);
        strip.getMeterStrip().setLevels(0, 0.88f, 0.65f);
        strip.getMeterStrip().setLevels(1, 0.82f, 0.60f);
        strip.addInsert();
        strip.addSend();
        std::cout << "  track='" << strip.getTrackLabel().getText() << "'" << std::endl;
        std::cout << "  inserts=" << strip.getInserts().size() << " sends=" << strip.getSends().size() << std::endl;
    }, demos);

    runDemo("Mixer::MixerPanel", []()
    {
        MixerPanel panel;
        auto drumStrip = std::make_shared<ChannelStrip>();
        drumStrip->getTrackLabel().setText("Drums");
        auto bassStrip = std::make_shared<ChannelStrip>();
        bassStrip->getTrackLabel().setText("Bass");
        panel.addChannelStrip(drumStrip);
        panel.addChannelStrip(bassStrip);
        std::cout << "  channel count=" << panel.getChannelStrips().size() << std::endl;
    }, demos);

    runDemo("Arrangement::Timeline & Canvas", []()
    {
        auto timeline = std::make_shared<TimelineRuler>();
        timeline->setZoom(1.5);

        auto header = std::make_shared<TrackHeader>();
        header->setTitle("Piano");

        auto clip = std::make_shared<ClipRegion>();
        clip->setColor(NUIColor::fromHex(0x38bdf8));
        clip->setLooped(true);

        ArrangementCanvas canvas;
        canvas.setTimeline(timeline);
        canvas.addTrackHeader(header);
        canvas.addClip(clip);

        std::cout << "  headers=" << canvas.getTrackHeaders().size()
                  << " clips=" << canvas.getClips().size() << std::endl;
        std::cout << "  clip looped=" << boolToString(canvas.getClips().front()->isLooped()) << std::endl;
    }, demos);

    runDemo("Arrangement::Automation & Selection", []()
    {
        AutomationCurve curve;
        curve.setPoints({ {0.0f, 0.0f}, {0.5f, 0.8f}, {1.0f, 0.2f} });

        GridLines grid;
        grid.setSpacing(0.25f);

        Playhead playhead;
        playhead.setPosition(32.5);

        SelectionBox selection;
        selection.setSelectionRect({ 10.0f, 20.0f, 400.0f, 120.0f });

        ZoomControls zoom;
        zoom.setZoom(0.75);
        zoom.setOnZoomChanged([](double factor)
        {
            std::cout << "  zoom changed -> " << factor << std::endl;
        });

        std::cout << "  automation points=" << curve.getPoints().size() << std::endl;
        std::cout << "  grid spacing=" << grid.getSpacing() << std::endl;
        std::cout << "  playhead position=" << playhead.getPosition() << std::endl;
        auto rect = selection.getSelectionRect();
        std::cout << "  selection rect=" << rect.width << "x" << rect.height << std::endl;
    }, demos);

    runDemo("Visual::Analyzers", []()
    {
        AudioVisualizer visualizer;
        const std::vector<float> visualizerData = { 0.0f, 0.3f, 0.6f, 0.3f, 0.0f, -0.3f, -0.6f };
        visualizer.setWaveformData(visualizerData);

        SpectrumAnalyzer spectrum;
        const std::vector<float> spectrumData = { 0.1f, 0.4f, 0.8f, 0.6f, 0.2f };
        spectrum.setSpectrumData(spectrumData);

        PhaseScope phase;
        const std::vector<NUIPoint> phasePoints = { {0.1f, 0.2f}, {0.3f, 0.4f}, {0.4f, 0.2f} };
        phase.setPhaseData(phasePoints);

        WaveformDisplay waveform;
        const std::vector<float> waveformData = { 0.0f, 0.2f, 0.4f, 0.2f, 0.0f };
        waveform.setWaveform(waveformData);

        VUBridge bridge;
        bridge.setLeftLevel(0.76f);
        bridge.setRightLevel(0.72f);

        std::cout << "  waveform samples=" << visualizerData.size() << std::endl;
        std::cout << "  spectrum bins=" << spectrumData.size() << std::endl;
        std::cout << "  phase points=" << phasePoints.size() << std::endl;
        std::cout << "  vu bridge L/R=" << bridge.getLeftLevel() << "/" << bridge.getRightLevel() << std::endl;
    }, demos);

    runDemo("Utility::Workflow", []()
    {
        DialogBox dialog;
        dialog.setTitle("Delete Track");
        dialog.setMessage("Are you sure you want to delete this track?");

        FileBrowser files;
        files.setCurrentPath("/Projects/Nomad");

        PluginBrowser plugins;
        plugins.setPlugins({ "NomadEQ", "NomadComp", "SpaceVerb" });

        SettingsPanel settings;
        settings.setCategories({ "Audio", "MIDI", "Appearance" });

        Tooltip tooltip;
        tooltip.setText("Drag to reorder tracks");

        NotificationToast toast;
        toast.setText("Project saved");
        toast.setDuration(2.5);

        ContextMenu context;
        context.setItems({ {"add", "Add Track", true}, {"remove", "Remove", true} });

        ModalOverlay overlay;
        overlay.setActive(true);

        std::cout << "  dialog title='" << dialog.getTitle() << "'" << std::endl;
        std::cout << "  current path=" << files.getCurrentPath() << std::endl;
        std::cout << "  plugins=" << plugins.getPlugins().size() << " categories=" << settings.getCategories().size() << std::endl;
        std::cout << "  tooltip='" << tooltip.getText() << "'" << std::endl;
        std::cout << "  overlay active=" << boolToString(overlay.isActive()) << std::endl;
    }, demos);

    runDemo("Thematic::Atmosphere", []()
    {
        SplashScreen splash;
        splash.setMessage("Welcome to Nomad");

        LoadingSpinner spinner;
        spinner.setSpeed(360.0);

        ThemeSelector selector;
        selector.setThemes({ "Nomad Dark", "Nomad Light", "Sunrise" });

        ReflectionPanel reflection;
        reflection.setContent("Remember: stay curious.");

        StatusBar status;
        status.setLeftText("Nomad Ready");
        status.setRightText("12:00:00");

        std::cout << "  splash message='" << "Welcome to Nomad" << "'" << std::endl;
        std::cout << "  spinner speed set" << std::endl;
        std::cout << "  theme options=" << selector.getThemes().size() << std::endl;
        std::cout << "  status left='" << status.getLeftText() << "' right='" << status.getRightText() << "'" << std::endl;
    }, demos);

    std::cout << "\nRegistered " << demos.size() << " widget demos:" << std::endl;
    for (const auto& name : demos)
    {
        std::cout << " - " << name << std::endl;
    }

    std::cout << "\nAll demos executed." << std::endl;
    return 0;
}
