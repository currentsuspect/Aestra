# Muse AI — Predictive Creative Assistant

**Status:** Internal — Post-Beta (v1.1 target, Q2 2027)
**Last Updated:** 2026-04-11
**Owner:** Dylan

---

## What Muse Is

Muse is a **predictive** AI assistant that augments the musician's creative process. It watches what the user is doing and suggests what they might do next. The musician always makes the final decision.

## What Muse Is NOT

- **Not generative.** Muse does not create music. It suggests continuations based on the user's existing patterns.
- **Not automatic.** Every suggestion requires user acceptance. Nothing is applied without confirmation.
- **Not a replacement.** Muse reduces creative friction. It does not replace creative thinking.

---

## Core Philosophy

> "Autocomplete for music, not a music generator."

The same philosophy as GitHub Copilot for code:
- Copilot doesn't write your code. It suggests completions based on your context.
- Muse doesn't write your music. It suggests patterns based on your context.
- Both preserve the creator's agency and identity.

**Why this matters for musicians:**
Hip-hop producers and electronic musicians are deeply anti-generative-AI right now because it threatens their identity as creators. Predictive AI is different:
- "AI made this beat" → threatens identity
- "AI helped me finish this beat" → augments identity

Muse is the second one. Always.

---

## Prediction Domains

### 1. Piano Roll Prediction

**Trigger:** User has written 2+ bars of notes in the piano roll.

**Suggests:**
- Next bar continuation based on melodic/harmonic patterns
- Rhythm variations (hi-hat rolls, snare patterns)
- Velocity humanization (natural variation across repeated notes)
- Scale-aware fills and transitions
- Complementary bass line for a given melody
- Counter-melody based on existing melodic contour

**UI:**
- Ghost notes appear in the piano roll (semi-transparent)
- User presses Tab to accept, Escape to dismiss
- Right-click to see alternative suggestions (up to 3)
- Suggestion quality indicator (confidence level shown subtly)

### 2. Mixer Suggestions

**Trigger:** User is mixing (adjusting levels, EQ, routing).

**Suggests:**
- Frequency masking detection ("808 and kick fighting at 60Hz")
- Gain staging advice ("track peaking before master, consider -3dB")
- Reverb send level norms for genre
- Compression settings based on source material
- Stereo width suggestions ("vocal is too wide, consider narrowing")
- Routing optimization ("this chain would benefit from a bus")

**UI:**
- Non-intrusive notification in mixer inspector
- "Muse suggests: ..." with one-click apply
- Dismissible with no penalty (Muse learns from dismissals)
- Appears only when confidence is high (>80%)

### 3. Arrangement Assist

**Trigger:** User is arranging patterns on the timeline.

**Suggests:**
- Pattern variation points ("pattern is 16 bars of the same — consider a fill at bar 8")
- Transition risers/downers at section boundaries
- Drop/break placement based on genre conventions
- Energy curve analysis ("energy is flat — consider a breakdown")
- Intro/outro length norms for streaming platforms

**UI:**
- Timeline markers with suggestion indicators
- Click to preview suggestion (non-destructive audition)
- Accept → applies to timeline via command (undoable)

---

## Technical Architecture

### Training Data Sources

| Source | Data | Privacy |
|--------|------|---------|
| Aestra usage (opt-in) | Piano roll edits, mixing decisions, arrangement patterns | Anonymized, aggregated |
| Public MIDI datasets | Melodic/harmonic patterns, genre conventions | Public domain |
| Audio analysis | Frequency content, dynamic range, stereo image | User's own projects only |

### Model Approach

**Phase 1 (v1.1):** Rule-based + statistical models
- Genre-specific pattern libraries
- Statistical analysis of user's own patterns
- Frequency masking detection via FFT analysis
- No deep learning required for initial launch

**Phase 2 (v1.2+):** Lightweight ML models
- Trained on anonymized Aestra usage data
- Per-user personalization (Muse learns YOUR style)
- Runs locally (no cloud inference for predictions)
- Small model size (<100MB) for fast inference

**Phase 3 (v2.0+):** Advanced models
- Transformer-based sequence prediction for piano roll
- Multi-modal (audio + MIDI + arrangement context)
- Community-trained models (genre-specific)

### Performance Requirements

| Constraint | Target |
|------------|--------|
| Inference time | <100ms for piano roll suggestions |
| CPU usage | <5% additional on target hardware (i5-3337U) |
| Memory | <200MB model loaded |
| Battery impact | Negligible (batch inference, not continuous) |

### Privacy Model

- Muse processes data **locally** — no audio or MIDI sent to cloud
- Opt-in anonymous usage telemetry for model improvement
- User can disable telemetry entirely (Muse still works, just doesn't improve)
- Clear privacy policy published before v1.1 launch

---

## Interaction Model

### Suggestion Lifecycle

```
User is working
    │
    ▼
Muse detects pattern context
    │
    ▼
Confidence > threshold?
    ├── No → Stay silent
    └── Yes → Show suggestion
                │
                ▼
          User sees ghost/suggestion
                │
                ├── Accept (Tab / click)
                │       └── Apply via CommandHistory (undoable)
                │
                ├── Dismiss (Escape / click away)
                │       └── Muse learns from dismissal
                │
                └── See alternatives (Right-click)
                        └── Show 2-3 alternatives
                            └── Pick one or dismiss
```

### Confidence Thresholds

| Domain | Min Confidence | Rationale |
|--------|---------------|-----------|
| Piano roll | 70% | Creative suggestions can be loose |
| Mixer | 85% | Mix suggestions must be reliable |
| Arrangement | 75% | Genre norms are suggestive, not strict |

### Dismissal Learning

When a user dismisses a suggestion, Muse records:
- The suggestion type
- The context (what the user was doing)
- Not the specific musical content (privacy)

Over time, Muse learns to stop suggesting things the user always dismisses. This makes Muse personal without requiring explicit configuration.

---

## Monetization

| Tier | Muse Access |
|------|------------|
| Core (Free) | None |
| Supporter ($5/mo) | Full Muse AI |
| Founder ($129) | Full Muse AI, lifetime |
| Campus (Free) | Full Muse AI |

Muse is the primary justification for the Supporter subscription. It must be genuinely useful or the subscription has no value prop beyond plugins.

---

## Quality Bar

**Do NOT ship Muse until:**
- Piano roll suggestions are accepted >30% of the time by test users
- Mixer suggestions have <10% false positive rate (bad advice)
- Arrangement suggestions feel genre-appropriate >80% of the time
- No performance regression on target hardware
- Privacy model is reviewed and approved

**If quality bar is not met by v1.1 target → delay to v1.2.** Bad AI is worse than no AI.

---

## Future Vision

### Muse v2.0 Ideas (Speculative)

- **Style transfer:** "Make this pattern sound more like [genre]"
- **Reference matching:** "Match the energy curve of this reference track"
- **Collaborative Muse:** Two producers working on the same Take, Muse suggests how to blend their styles
- **Teaching mode:** Muse explains WHY it suggests something ("this resolution follows the circle of fifths")
- **Voice interaction:** "Muse, try a darker variation" (long-term, post-v2)

### What Muse Never Becomes

- A music generator
- A replacement for human creativity
- A tool that applies changes without user confirmation
- A cloud-dependent service (always local-first)

---

*This document is internal. Muse is post-Beta. Do not promise publicly until v1.0 launch.*
