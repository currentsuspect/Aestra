# AestraUI (NUI)

NUI is the DAW's custom C++17 UI framework, living in `AestraUI/`. Everything on screen is an `NUIComponent` drawn through the `NUIRenderer` interface; the only backend in the tree is the OpenGL renderer `NUIRendererGL`.

## Directory layout

- `AestraUI/Base/` — generic controls (`NUIButton.h`, `NUISlider.h`, `NUILabel.h`, `NUIDropdown.h`, `NUITextInput.h`, `NUICheckbox.h`, `NUIProgressBar.h`, `NUIScrollbar.h`, `NUIContextMenu.h`, and others).
- `AestraUI/Common/` — shared helpers (`MusicHelpers.h`).
- `AestraUI/Config/` — YAML/text config data (slider, title bar).
- `AestraUI/Core/` — component tree, themes, app loop, animation, drag and drop (`NUIComponent.h`, `NUITheme.h`, `NUIApp.h`, `NUIAnimation.h`, `NUIDragDrop.h`, `NUIAdaptiveFPS.h`, `NUITypes.h`).
- `AestraUI/External/` — vendored third-party code (GLAD loader, ThorVG, FreeType, stb headers).
- `AestraUI/Graphics/` — the `NUIRenderer` interface plus SVG helpers (`NUISVGParser.h`, `NUISVGCache.h`); the only backend subfolder is `AestraUI/Graphics/OpenGL/`.
- `AestraUI/Helpers/` — view helpers for piano roll, timeline grid, and mixer plugin lists.
- `AestraUI/Layout/` — the header-only V8-X2b layout subsystem (`NUILayoutNode.h`, `NUILayoutAlgorithms.h`, `NUILayoutSpace.h`, `NUIAnchoredPlacement.h`). It is adopted per surface, not built into `NUIComponent`.
- `AestraUI/Platform/` — `NUIPlatformBridge`, which wraps `AestraPlat` windows for NUI, plus the cursor service (`NUICursorService.h`).
- `AestraUI/Widgets/` — DAW views: mixer strips, piano roll, arrangement, transport, and plugin editors.

## Core concepts

- **Components.** `NUIComponent` (`AestraUI/Core/NUIComponent.h`) is the base class: a parent/child tree with absolute bounds (`setBounds`, `getBounds`), visible/enabled/focus/hover state, opacity, `NUILayer` ordering, tooltips, and a dirty flag (`setDirty`, `isDirty`). Subclasses override `onRender`, `onUpdate`, `onMouseEvent`, and `onKeyEvent`. Mouse dispatch goes through the static `dispatchMouseEvent`, not direct `onMouseEvent` calls.
- **Renderer.** `NUIRenderer` (`AestraUI/Graphics/NUIRenderer.h`) is a pure-virtual interface: rect/circle/line primitives, `fillWaveform`, linear and radial gradients, `drawGlow`/`drawShadow`, `drawText`/`drawTextCentered`/`measureText`, texture upload and drawing (`loadTexture`, `createTexture`, `drawTexture`), scissor clips, opacity, a transform stack (`pushTransform`/`popTransform`), and explicit batching (`beginBatch`, `endBatch`, `flush`). `NUIRendererGL` (`AestraUI/Graphics/OpenGL/NUIRendererGL.h`) is the only implementation; `getBackendName` returns `"OpenGL 3.3+"`. Text is a FreeType bitmap glyph atlas.
- **Events.** `NUIMouseEvent` and `NUIKeyEvent` (`AestraUI/Core/NUITypes.h`) carry `pressed`/`released` flags; key codes use `NUIKeyCode` (for example `NUIKeyCode::Enter`). Components also expose `onMouseDown`/`onMouseUp`/`onMouseMove`/`onMouseWheel`/`onKeyDown`/`onKeyUp` callbacks.
- **Layout.** `NUIComponent` has no layout engine: bounds are set with `setBounds`, and `AestraUI/Core/NUITypes.h` provides placement helpers (`NUIAbsolute`, `NUIStackHorizontal`, `NUIStackVertical`, `NUIGridCell`). Surfaces migrated to the V8-X2b layout contract compute their bounds with the `Layout/` headers instead: `Source/Panels/WindowPanel.cpp` and `AestraUI/Widgets/UIMixerPanel.cpp` use `NUILayoutAlgorithms.h` and `NUILayoutSpace.h`, and `Source/Core/UISurfaceResolution.h` uses `NUIAnchoredPlacement.h`. `NUILayoutNode.h` has no consumer outside `Layout/` and its tests yet.
- **Themes.** `NUITheme` (`AestraUI/Core/NUITheme.h`) holds named colors (`getBackground`, `getSurface`, `getPrimary`, `getText`, …), dimensions (`getBorderRadius`, `getPadding`, …), effects (`getGlowIntensity`, …), and font sizes (`getFontSizeTitle`, …). `createDefault` builds the dark theme; `loadFromFile` loads one from JSON. Components inherit their parent's theme when they have none set, and `onThemeChanged` propagates refreshes to children.
- **App loop.** `NUIApp` (`AestraUI/Core/NUIApp.h`) owns the renderer and the root component: `initialize(width, height, title)`, `setRootComponent`, `run` (blocks until `quit`), plus frame timing (`getDeltaTime`, `getCurrentFPS`) and the `NUIAdaptiveFPS` manager.
- **Animation and drag and drop.** `NUIAnimation`/`NUIAnimationManager` (`AestraUI/Core/NUIAnimation.h`) and `NUIDragDropManager` (`AestraUI/Core/NUIDragDrop.h`) exist in `Core/`; easing helpers also live in `AestraUI/Core/NUITypes.h` (`NUIAnimationCurve`, `NUIEasing`).

## Build and link

`AestraUI/CMakeLists.txt` requires CMake >= 3.22 and compiles C++17. It defines three static libraries plus one umbrella target:

- `AestraUI_Core` — `Core/`, `Base/`, most of `Widgets/`, and the non-GL `Graphics/` sources. It exposes `AestraUI/`, `Core/`, `Base/`, `Widgets/`, `Graphics/`, and `Common/` as public include directories, so consumers include headers bare (for example `"NUIComponent.h"`).
- `AestraUI_OpenGL` — `Graphics/OpenGL/` (`NUIRendererGL`, `NUIRenderCache`, `NUIDirtyRegionManager`). Enabled by `AESTRAUI_BUILD_OPENGL` (default ON).
- `AestraUI_Platform` — the `AestraPlat` bridge. Links `AestraUI_Core` and `AestraPlat`.
- `AestraUI` — INTERFACE target combining the above.

Options: `AESTRAUI_BUILD_OPENGL` (default ON) and `AESTRAUI_ENABLE_PREMIUM_EDITORS` (default OFF). The root `CMakeLists.txt` only adds `AestraUI` when `AESTRA_ENABLE_UI` is on, and `AESTRA_HEADLESS_ONLY` forces it off, so headless/CI builds skip the UI entirely.

Dependencies: FreeType is required (configure errors out if neither system FreeType nor `AestraUI/External/freetype_local` is present). GLAD is built in-tree as `glad`. ThorVG is vendored for SVG. SDL2 is optional — when found, `AestraUI_OpenGL` links it and defines `AestraUI_SDL2_AVAILABLE`.

## Minimal example

```cpp
#include <memory>
#include "NUIApp.h"
#include "NUIComponent.h"
#include "NUIRenderer.h"
#include "NUITheme.h"

class MyComponent : public AestraUI::NUIComponent {
public:
    void onRender(AestraUI::NUIRenderer& renderer) override {
        auto theme = getTheme();
        if (!theme) {
            return;
        }
        renderer.fillRoundedRect(getBounds(), theme->getBorderRadius(), theme->getSurface());
        renderer.drawTextCentered(
            "Hello, NUI!", getBounds(), theme->getFontSizeTitle(), theme->getText());
    }

    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override {
        if (event.pressed) {
            return true;
        }
        return false;
    }
};

int main() {
    AestraUI::NUIApp app;
    if (!app.initialize(800, 600, "My NUI App")) {
        return 1;
    }

    auto root = std::make_shared<MyComponent>();
    root->setBounds(0, 0, 800, 600);
    root->setTheme(AestraUI::NUITheme::createDefault());
    app.setRootComponent(root);

    app.run();
    return 0;
}
```

## Known limitations and placeholders

- `NUIRendererGL::drawGlow` is not a shader glow: it fills an expanded translucent rect (`AestraUI/Graphics/OpenGL/NUIRendererGL.cpp`).
- The widget render cache (`NUIRenderCache`, toggled with `setCachingEnabled`) exists and has real consumers, but the application disables it on Linux (`Source/Core/AestraWindowManager.cpp`). Widgets must render correctly with the cache off — losing it may only cost frame rate, never content.
- `NUIDirtyRegionManager` (`AestraUI/Graphics/OpenGL/NUIDirtyRegion.h`) is present but has no working consumer: the only `markAllDirty` call in `beginFrame` is commented out, nothing else marks regions, and no render pass skips clean regions. Treat dirty-region tracking as inert scaffolding, not an optimization.
- Text rendering is a FreeType bitmap atlas. There is no SDF or MSDF text renderer in the tree.
- There is no Vulkan backend — `Graphics/` contains only `OpenGL/` — and no `Win32`/`Cocoa`/`X11` platform layer: windowing goes through `NUIPlatformBridge` on top of `AestraPlat`.
- The V8-X2b layout contract covers only migrated surfaces — `WindowPanel`'s title-bar buttons, and the mixer's master/inspector placement and scrolling strip row; every other surface still positions its children by hand.

## Tests

UI tests live in `Tests/AestraUI/`, including `NUIThemeJSONTest.cpp` (theme JSON loading), `NUILayoutCoreTest.cpp`, `NUILayoutAlgorithmsTest.cpp`, `NUIAnchoredPlacementTest.cpp`, and component interaction tests (dropdown, context menu, tooltip, text input, piano roll).
