# NomadUI Widget Demo Catalog

The `NomadUI_WidgetCatalogueDemo` console target instantiates every widget in the NomadUI catalogue and exercises its public configuration API. Use it as a smoke test and quick reference while developing new UI features.

## Building and running

1. Configure the NomadUI project with examples enabled (default):
   ```bash
   cmake -S . -B build
   ```
2. Build the widget demo target:
   ```bash
   cmake --build build --target NomadUI_WidgetCatalogueDemo
   ```
3. Execute the binary to print the widget demo summary:
   ```bash
   ./build/bin/NomadUI_WidgetCatalogueDemo
   ```

## Demo coverage

The demo runs a lightweight scenario for each widget. The console output lists every entry in the order below:

### Core primitives
- `Core::NUIToggle` – toggles between on/off/disabled and logs callbacks.
- `Core::NUITextField` – configures user text with placeholder support.
- `Core::NUIMeter` – sets stereo peak/RMS levels and decay configuration.
- `Core::NUIScrollView` – clamps offsets for large scrollable content.
- `Core::NUIPanel` – showcases background, border, and variant selection.
- `Core::NUIPopupMenu` – registers menu items and selection callbacks.
- `Core::NUITabBar` – manages tab creation, activation, and removal.

### Transport controls
- `Transport::Play/Record/Stop` – demonstrates button state toggles and callbacks.
- `Transport::Displays` – updates tempo, signature, clock, and performance meters.
- `Transport::TransportBar` – wires together the complete control surface.

### Mixer and channel strip
- `Mixer::Channel Controls` – configures fader, pan, toggles, inserts, and meters.
- `Mixer::MixerPanel` – aggregates multiple channel strips.

### Arrangement and editing
- `Arrangement::Timeline & Canvas` – attaches timeline, headers, and clips.
- `Arrangement::Automation & Selection` – adjusts automation points, grid, selection, and zoom.

### Visualization
- `Visual::Analyzers` – feeds waveform, spectrum, phase, waveform display, and VU bridge data.

### Utility and overlays
- `Utility::Workflow` – covers dialogs, browsers, tooltips, toasts, context menus, and overlays.

### Thematic polish
- `Thematic::Atmosphere` – tests splash screen, spinner, themes, reflections, and status bar.

Each section prints a concise log indicating the widget state that was exercised so you can confirm wiring and extend tests as new behaviors land.
