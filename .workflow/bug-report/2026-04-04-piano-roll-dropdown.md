---
id: "BRIEF-002"
type: "bug"
severity: "P2"
date: "2026-04-04"
status: "complete"
---

## User Input
> "in piano roll i can't change scale, snap etc because something is wrong with the dropdown, probably a z issue, check and fix"

## Context
- Relevant files:
  - `AestraUI/Widgets/NUIPianoRollWidgets.cpp:469-518` — PianoRollToolbar menu button creates NUIContextMenu
  - `AestraUI/Widgets/NUIPianoRollWidgets.cpp:515-516` — Menu positioned using button's local bounds: `menu->showAt(b.x, b.y + b.height + 2.0f)`
  - `AestraUI/Widgets/NUIPianoRollWidgets.cpp:518` — Menu added as child of toolbar: `addChild(menu)`
  - `AestraUI/Base/NUIContextMenu.cpp:134` — Hit-testing uses `getGlobalBounds()`: `NUIRect globalBounds = getGlobalBounds()`
  - `AestraUI/Base/NUIContextMenu.cpp:309-339` — `showAt(int x, int y)` sets position using passed coordinates directly
  - `AestraUI/Core/NUIComponent.cpp:208-218` — `getGlobalBounds()` accumulates parent offsets
- Affected layer: UI
- Current behavior: Clicking the hamburger menu button in PianoRollToolbar shows the context menu visually, but clicking menu items (Scale, Snap, Root Key) does nothing — clicks are not registered
- Expected behavior: Menu items respond to clicks and change scale/snap settings

## Root Cause Hypothesis
**Coordinate system mismatch between positioning and hit-testing.**

The menu is positioned with `showAt(b.x, b.y + b.height + 2.0f)` where `b` is the button's **local bounds** (relative to the toolbar). These are small values like `x=10, y=5`. The menu's `setBounds` is set to these coordinates.

But `onMouseEvent` uses `getGlobalBounds()` for hit-testing, which accumulates parent offsets. Since the menu is a child of the toolbar (which itself is a child of PianoRollView), the global bounds will be something like `x=100, y=50` (toolbar position + toolbar offset).

The result: the menu renders at local position `(10, 7)` but hit-testing checks against global bounds `(100, 50)`. Mouse events at the visual menu location don't match the global bounds, so clicks are never consumed.

Additionally, submenus have the same issue — they're positioned relative to their parent menu but hit-tested against global bounds.

## Suggested Approach
Two possible fixes:

**Option A (Recommended): Fix hit-testing to use local coordinates.**
- In `NUIContextMenu::onMouseEvent`, convert the event position from global to local coordinates before checking bounds, OR use `getBounds()` instead of `getGlobalBounds()` since mouse events are already transformed to the component's coordinate space by the parent's event dispatch.

**Option B: Fix positioning to use global coordinates.**
- In `PianoRollToolbar::setupUI`, use `m_menuBtn->getGlobalBounds()` instead of `m_menuBtn->getBounds()` when positioning the menu, then convert back to local coordinates for `showAt`.

Option A is cleaner — the NUI event system typically delivers events in the component's local coordinate space, so `getBounds()` should be the right check. The `getGlobalBounds()` usage appears to be a bug introduced when the context menu was designed for screen-space (popup) usage but is being used as a child component.

## Risks
- Changing hit-testing in NUIContextMenu could affect other uses of the component (PluginBrowserPanel, etc.)
- Need to verify that mouse events are indeed delivered in local coordinates by the parent dispatch system
- Submenu positioning may also need adjustment

## Acceptance Criteria
- [ ] Scale dropdown responds to clicks and changes scale
- [ ] Snap dropdown responds to clicks and changes snap
- [ ] Root Key dropdown responds to clicks and changes root key
- [ ] Submenus open and items are clickable
- [ ] No regression in other NUIContextMenu usages (PluginBrowserPanel, etc.)

## Resolution
- **What was done**: Three-part fix across NUIContextMenu and PianoRollToolbar:
  1. `PianoRollToolbar::setupUI`: Use `getBounds()` (local coords) for menu positioning — menu is a child of toolbar so local coords are correct
  2. `NUIContextMenu::showAt`: Clamp against immediate parent's size (not root parent's bounds with offsets) to prevent position warping
  3. `NUIContextMenu::onMouseEvent`: Use `getGlobalBounds()` for containment check, then convert global event position to local by subtracting the menu's global bounds origin before calling `getItemAtPosition`
  4. `NUIContextMenu::getItemAtPosition`: Accept local coordinates and check against local bounds instead of global bounds
- **What differed from brief**: First attempt used `getGlobalBounds()` for positioning which spawned menu at wrong location. Second attempt converted all coords to local which broke playhead rendering. Final fix: keep positioning local, keep hit-testing global, convert only for item lookup.
- **Commits**: 7c2b1ae9 (reverted), 3e89ada6 (partial), final commit pending
- **Build**: lowmem build passes cleanly
