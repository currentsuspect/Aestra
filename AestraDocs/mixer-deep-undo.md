# Mixer Deep Undo — Command Wiring
Date: 2026-04-13

## What Works
- Volume fader → SetVolumeCommand ✅ (wired in UIMixerPanel)

## What Needs Wiring (commands exist, just need callback)
In UIMixerStrip.cpp, find the mute/solo/pan callbacks and add CommandHistory.pushAndExecute():

### Mute button
The mute button callback already fires via `onMuteToggled`. Find it in UIMixerStrip.cpp and add:
```cpp
m_trackManager->getCommandHistory().pushAndExecute(
    std::make_shared<SetMuteCommand>(*mixerChannel, newState));
```

### Solo button
Same pattern with SetSoloCommand.

### Pan knob
Same pattern with SetPanCommand.

### Trim knob
Same pattern — may need a new SetTrimCommand if it doesn't exist.

## What Needs New Commands
- **AddPluginCommand** — insert a plugin on a channel slot
- **RemovePluginCommand** — remove a plugin from a channel slot
- **SetSendGainCommand** — change send gain
- **SetSendDestinationCommand** — change send destination
- **AddTrackCommand** — create a new mixer track
- **RemoveTrackCommand** — remove a mixer track

## Where to Wire
UIMixerStrip.cpp has callbacks for:
- m_muteButton → onMuteToggled
- m_soloButton → onSoloToggled
- m_panKnob → onValueChanged
- m_trimKnob → onValueChanged
- m_fader → onValueChanged (already wired)

All of these need CommandHistory.pushAndExecute() with the existing commands (SetMuteCommand, SetSoloCommand, SetPanCommand).

## Key Insight
After pushing the command, the ViewModel needs to be synced. The current volume wiring works because `refreshChannels()` syncs `faderGainDb` from the actual channel. The same approach applies to mute/solo/pan — after undo, `refreshChannels()` should sync ALL channel state from the MixerChannel, not just volume.
