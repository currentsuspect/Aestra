# Screenshot Limitation with Borderless Windows [RESOLVED]

## Issue Description
Previously, the NOMAD DAW used `WS_POPUP` for a borderless design, which prevented Windows screenshot tools from detecting the window correctly.

## Resolution
We have migrated to **`WS_OVERLAPPEDWINDOW`** combined with **`WM_NCCALCSIZE` handling**. This technique allows us to:
1.  Remove the standard Windows title bar and borders visually (preserving the professional look).
2.  Retain the underlying standard window behavior.

## Results
- ✅ **Screenshots Work**: `Win + Shift + S` and `Print Screen` now capture the window correctly.
- ✅ **Standard Behavior**: Snap layouts, maximize/minimize animations, and taskbar interactions work as expected.
- ✅ **Borderless Look**: The custom UI remains virtually identical to the previous implementation.

---

*Document updated: January 2026*
*Status: **RESOLVED***
