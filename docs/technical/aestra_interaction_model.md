# Aestra Interaction & Intent Model

Owner's design note, 18 Nov 2026. Aestra should understand what the user is talking about, what they are trying to do, and what the next interaction is likely to mean. Behaviour should follow predictable, composable rules, not per-panel conventions.

Companion to `arsenal_coherence_spec.md`. This page is split into what the **Arsenal coherence run (PR #1449)** takes on, and what is **kept for later**. More items will be named by the owner on Saturday 21 Nov.

---

## A. For the Arsenal coherence run

### A1. Keys go to the surface the user is working in (bug)
**Observed:** with the Arsenal as the active view and the Piano Roll in use, Delete removes Arsenal units instead of piano-roll notes.

**Cause (traced):** `AestraRootComponent::onKeyEvent` and `AestraWindowManager` send Escape, Delete, Backspace and F2 to `AestraContent` before the focused widget, and `AestraContent` gives them to the Arsenal whenever `m_viewFocus == Arsenal`. `PianoRollPanel` never takes widget focus, so nothing says the Piano Roll is the surface in use. The Ctrl+A path already checks the focused component; Delete does not, and a focus check alone is not enough while the Piano Roll cannot hold focus.

**Rule (§1.2):** focus decides where a key is interpreted. Delete, Backspace, Escape, F2 and Ctrl+A act on the surface last pressed in. A click in the Piano Roll makes it that surface. The Arsenal handles these keys only when it is that surface.

### A2. One selection inside the Arsenal (§1.1, Arsenal only)
Units and patterns are already mutually exclusive in #1449. Keep it so: one active selection kind at a time; selecting the other kind clears the first.

### A3. Double-click resets to default, Swing only (§3)
Swing resets to 0% on double-click and only that control changes. Done in #1449. Double-click reset elsewhere is in section B.

### A4. Intent preservation inside the Arsenal (§8)
Browsing a kit creates no units, samples or patterns. Creating a unit makes exactly its pattern and mixer route, as one undo step. Already in #1449.

### A5. Candidates, awaiting the owner's word
From the earlier Arsenal notes: mute and solo are not in undo or the history panel; uniform 2 px spacing and no overlapping ("bleeding") elements inside the Arsenal. Not started; to be confirmed on Saturday.

---

## B. Kept for later (not in this run)

- **Cross-surface selection model (§1.1 whole, §1.3, §1.4, §1.5).** One active selection across the Arsenal, File Browser and Piano Roll; selection that trails the user's focus and transfers only on an explicit select; related views such as the Piano Roll editing Pattern A keep Pattern A selected.
- **Focus versus selection as a shared state model (§2).** The Arsenal only gets the routing fix in A1.
- **Canonical defaults app-wide (§3).** Double-click reset for track height, panel size, split position and parameters. It is not Undo, and it is local to the element.
- **Interaction vocabulary (§4).** Select, Move, Resize, Double-click, Delete, Duplicate, Escape, Enter, F2, Ctrl+Z as one language across panels, including Enter acting on the selected item.
- **Panel navigation (§5).** Primary click goes to the panel (open, bring forward, or stay); secondary click controls visibility.
- **Panel state visuals (§6) and accent as meaning (§7).** Closed, open behind, and frontmost shown differently; the primary accent used only for "active / selected / here".
- **Predictability over DAW convention (§9).**
- **Design test (§11).** Can the user predict it, understand why it happened, and did only the intended thing change?

## C. Unresolved on purpose (§10)

Arsenal mode versus Piano Roll intent: from Timeline, the Arsenal button opens the Arsenal; from an Arsenal-associated Piano Roll the user may mean "work in this context". **Do not add conditional behaviour or a preference.** Revisit after the broader model exists. The user should not need to understand internal modes to express intent.

## Design principle

Aestra should feel like one instrument. I select what I'm talking about. I focus where I'm working. Double-click brings adjustable things home. Clicking a destination takes me there. The interface shows what is active. My actions change only what I intended.
