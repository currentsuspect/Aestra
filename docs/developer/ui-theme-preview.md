# Previewing a theme or colour change in the real renderer

A browser mock never matches Aestra: the font rasteriser, the blending, the layout and the alpha behaviour all
differ, so a colour or surface idea is judged from the app itself. `tools/ui/theme_preview.sh` starts the app on a
private display with a theme (and optional token overrides), drives it to named scenes, saves each as a PNG and lays
several runs side by side on a contact sheet.

It reuses the tools from `tools/ui/` (`run.sh` starts the app silently with its own HOME and display, `drive.py` sends
real pointer and key events and takes the screenshots). Nothing touches the real desktop, audio or settings.

## Run one option

```sh
# the current Ink, every scene
tools/ui/theme_preview.sh run --label A --theme ink-dark --out /tmp/preview

# an idea as token overrides on top of the theme
tools/ui/theme_preview.sh run --label C --theme ink-dark --out /tmp/preview \
    --set primary=#764bdf --set selected=#764bdf --set textOnPrimary=#ffffff \
    --scenes arsenal-4rows,context-menu,mixer

# the same overrides from a file ({"colors": {...}})
tools/ui/theme_preview.sh run --label D --overrides d.json --out /tmp/preview
```

Each run writes `/tmp/preview/<label>/<scene>.png` (1366 x 768) and the `overrides.json` it used.
`tools/ui/theme_preview.sh list` prints the scene names: `arsenal-4rows`, `context-menu`, `arsenal-pianoroll`,
`file-browser` (also saves `file-browser-sample`), `sample-editor`, `mixer`, `timeline`, `clip-editor`, `patterns`,
`audition`, `versions`, `settings`.

Keys in `--set` are theme property names: `primary`, `selected`, `textOnPrimary`, `backgroundPrimary` (the bed),
`backgroundSecondary` (the card), `surfaceTertiary`, `surfaceRaised`, `textPrimary`, and the rest of the list in
`AestraUI/Core/NUIThemeSystem.cpp` (`applyJSONThemeOverrides`). Setting `primary` alone derives `selected` as an 18%
wash, so for a solid selection set both. The overrides reach the app through the `AESTRA_THEME_OVERRIDES` environment
variable (a theme JSON laid over the chosen theme at start-up, `Source/Core/AestraWindowManager.cpp`); it is unset in
normal use.

## Contact sheet

```sh
tools/ui/theme_preview.sh sheet --out /tmp/preview --labels A,B,C,D
```

writes `/tmp/preview/sheet-<scene>.png` for every scene the labels share: each label's screenshot at its native size,
side by side, with a label bar. Nothing is scaled.

## A change that is code, not a token

Build the variant, then point the run at its binary with `--bin path/to/Aestra` (the binary needs its `AestraAssets`
folder beside it). The label is only a name.

## Notes

- Each run starts from an empty state folder (`<out>/.state`), so the project is a fresh "Untitled" and the browser
  shows four generated samples under `Documents/Aestra/Samples`.
- The scenes click fixed pixel positions on the 1366 x 768 display `run.sh` creates; a layout change can move a
  target, and the tool then captures whatever is there. Look at the PNGs.
- Needs `python-xlib`, `Pillow`, `Xvfb` and `bubblewrap`. The display defaults to `:77`; use `--display :79` if another
  session holds it.
