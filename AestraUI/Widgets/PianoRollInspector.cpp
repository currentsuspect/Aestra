// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PianoRollInspector.h"

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollLook.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
constexpr float kPad = 16.0f;
constexpr float kBarH = 6.0f;
} // namespace

PianoRollInspector::Summary PianoRollInspector::summarize() const {
    Summary s;
    auto layer = layer_.lock();
    if (!layer) return s;
    double first = 0.0, last = 0.0, velocitySum = 0.0, chanceSum = 0.0;
    float v0 = 0.0f, c0 = 1.0f;
    int r0 = 1;
    for (const auto& n : layer->getNotes()) {
        if (!n.selected || n.isDeleted) continue;
        if (s.count == 0) {
            first = last = n.startBeat;
            v0 = n.velocity;
            c0 = n.chance;
            r0 = n.ratchet;
            s.ratchet = n.ratchet;
        }
        ++s.count;
        first = std::min(first, n.startBeat);
        last = std::max(last, n.startBeat);
        velocitySum += n.velocity;
        chanceSum += n.chance;
        // Velocity is shown in MIDI steps, so differences under one step are not "mixed".
        if (std::abs(n.velocity - v0) * 127.0f >= 0.5f) s.velocityMixed = true;
        if (std::abs(n.chance - c0) >= 0.005f) s.chanceMixed = true;
        if (n.ratchet != r0) s.ratchet = 0;
    }
    if (s.count > 0) {
        s.velocity = static_cast<float>(velocitySum / s.count);
        s.chance = static_cast<float>(chanceSum / s.count);
        s.firstBar = static_cast<int>(first / beatsPerBar_) + 1;
        s.lastBar = static_cast<int>(last / beatsPerBar_) + 1;
    }
    return s;
}

// Layout, top to bottom (board A + C): SELECTION (11), the count (18), the bars (12), then Velocity, Chance and
// Ratchet, each a 12 px label row over its control.
NUIRect PianoRollInspector::velocityBar() const {
    const auto b = getBounds();
    return NUIRect(b.x + kPad, b.y + kPad + 90.0f + 18.0f, b.width - 2.0f * kPad, kBarH);
}
NUIRect PianoRollInspector::chanceBar() const {
    const auto v = velocityBar();
    return NUIRect(v.x, v.y + 36.0f, v.width, kBarH);
}
NUIRect PianoRollInspector::ratchetButton(int hits) const {
    const auto c = chanceBar();
    const float gap = 2.0f;
    const float w = (c.width - 3.0f * gap) / 4.0f;
    const int i = std::clamp(hits, 1, 4) - 1;
    return NUIRect(c.x + static_cast<float>(i) * (w + gap), c.y + 30.0f + 18.0f, w, 28.0f);
}

NUIRect PianoRollInspector::ideaButton(int index) const {
    const auto r = ratchetButton(1);
    const auto b = getBounds();
    // Under the ratchet row: a rule, the section title, then one 34 px row per idea (board A + C).
    const float top = r.bottom() + 24.0f + 30.0f;
    return NUIRect(b.x + kPad, top + static_cast<float>(index) * 40.0f, b.width - 2.0f * kPad, 34.0f);
}

float PianoRollInspector::barValueAt(const NUIRect& bar, float x) const {
    return bar.width <= 0.0f ? 0.0f : std::clamp((x - bar.x) / bar.width, 0.0f, 1.0f);
}

void PianoRollInspector::applyToSelection(Drag what, float value) {
    auto layer = layer_.lock();
    if (!layer) return;
    const auto& notes = layer->getNotes();
    for (size_t i = 0; i < notes.size(); ++i) {
        if (!notes[i].selected || notes[i].isDeleted) continue;
        layer->setLaneValue(static_cast<int>(i), what == Drag::Velocity ? NoteLaneField::Velocity : NoteLaneField::Chance,
                            value);
    }
    repaint();
}

void PianoRollInspector::setRatchet(int hits) {
    auto layer = layer_.lock();
    if (!layer || summarize().count == 0) return;
    const auto before = layer->getNotes();
    const auto& notes = layer->getNotes();
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].selected && !notes[i].isDeleted)
            layer->setLaneValue(static_cast<int>(i), NoteLaneField::Ratchet, static_cast<float>(hits));
    layer->pushExternalEdit(before, "Ratchet"); // one undo step
    repaint();
}

void PianoRollInspector::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    // Repaint only when what the inspector shows has changed: the selection or its values.
    const Summary s = summarize();
    if (!(s == lastShown_)) {
        lastShown_ = s;
        repaint();
    }
}

bool PianoRollInspector::onMouseEvent(const NUIMouseEvent& event) {
    const auto b = getBounds();
    if (!b.contains(event.position) && drag_ == Drag::None) return false;
    auto layer = layer_.lock();
    const auto grow = [](NUIRect r) { return NUIRect(r.x, r.y - 6.0f, r.width, r.height + 12.0f); };

    if (event.pressed && event.button == NUIMouseButton::Left && ideas_ && museMode_ != MuseMode::Off) {
        for (int i = 0; i < static_cast<int>(ideas_->size()); ++i)
            if (ideaButton(i).contains(event.position)) {
                if (onIdeaClicked_) onIdeaClicked_(i);
                return true;
            }
    }
    if (event.pressed && event.button == NUIMouseButton::Left && layer && summarize().count > 0) {
        for (int hits = 1; hits <= 4; ++hits) {
            if (ratchetButton(hits).contains(event.position)) {
                setRatchet(hits);
                return true;
            }
        }
        const Drag what = grow(velocityBar()).contains(event.position) ? Drag::Velocity
                          : grow(chanceBar()).contains(event.position) ? Drag::Chance
                                                                       : Drag::None;
        if (what != Drag::None) {
            drag_ = what;
            dragBefore_ = layer->getNotes();
            applyToSelection(what, barValueAt(what == Drag::Velocity ? velocityBar() : chanceBar(), event.position.x));
            return true;
        }
    }
    if (drag_ != Drag::None) {
        if (event.released) {
            if (layer) layer->pushExternalEdit(dragBefore_, drag_ == Drag::Velocity ? "Velocity" : "Chance");
            drag_ = Drag::None;
            dragBefore_.clear();
            return true;
        }
        applyToSelection(drag_, barValueAt(drag_ == Drag::Velocity ? velocityBar() : chanceBar(), event.position.x));
        return true;
    }
    // Only a press on the column is the column's: a move or release passing over it belongs to whatever drag is under
    // way (a loop zone dragged off the ruler, a note dragged right), which must keep tracking.
    return event.pressed;
}

void PianoRollInspector::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto& theme = NUIThemeManager::getInstance();
    const auto b = getBounds();
    const auto primary = theme.getColor("textPrimary");
    const auto secondary = theme.getColor("textSecondary");
    const auto muted = theme.getColor("textMuted");
    const auto bed = theme.getColor("backgroundPrimary");
    const auto raised = theme.getColor("surfaceRaised");
    renderer.fillRect(b, theme.getColor("backgroundSecondary"));
    renderer.drawLine(NUIPoint(b.x + 0.5f, b.y), NUIPoint(b.x + 0.5f, b.bottom()), 1.0f, theme.getColor("border"));

    auto layer = layer_.lock();
    const NUIColor unit = layer ? layer->getNoteColor() : theme.getColor("accentPrimary");
    const Summary s = summarize();
    const bool any = s.count > 0;
    const float x = b.x + kPad;
    float y = b.y + kPad;

    renderer.drawText("SELECTION", NUIPoint(x, y), 11.0f, muted);
    y += 20.0f;
    const std::string count = !any ? std::string("Nothing selected")
                                   : std::to_string(s.count) + (s.count == 1 ? " note" : " notes");
    renderer.drawText(count, NUIPoint(x, y), 18.0f, any ? primary : muted);
    y += 28.0f;
    const std::string where = !any ? std::string("Click a note or drag a box")
                              : s.firstBar == s.lastBar ? "Bar " + std::to_string(s.firstBar)
                                                        : "Bars " + std::to_string(s.firstBar) + "\xE2\x80\x93" + std::to_string(s.lastBar);
    renderer.drawText(where, NUIPoint(x, y), 12.0f, secondary);

    // A value row: the label left, the value right, the bar under them.
    const auto valueRow = [&](const NUIRect& bar, const char* label, const std::string& value, float fill, bool striped) {
        const float ly = bar.y - 18.0f;
        renderer.drawText(label, NUIPoint(bar.x, ly), 12.0f, secondary);
        const float vw = renderer.measureText(value, 12.0f).width;
        renderer.drawText(value, NUIPoint(bar.right() - vw, ly), 12.0f, any ? primary : muted);
        renderer.fillRect(bar, bed);
        if (!any) return;
        const NUIRect filled(bar.x, bar.y, bar.width * std::clamp(fill, 0.0f, 1.0f), bar.height);
        renderer.fillRect(filled, unit);
        if (striped)
            for (const auto& seg : PianoRollLook::chanceStripes(filled, 7.0f))
                renderer.drawLine(seg.a, seg.b, 2.5f, PianoRollLook::chanceStripe(unit));
    };
    const std::string velocity = !any ? std::string("\xE2\x80\x94")
                                 : s.velocityMixed ? std::string("Mixed")
                                                   : std::to_string(static_cast<int>(std::lround(s.velocity * 127.0f)));
    valueRow(velocityBar(), "Velocity", velocity, s.velocity, false);
    const std::string chance = !any ? std::string("\xE2\x80\x94")
                               : s.chanceMixed ? std::string("Mixed")
                                               : std::to_string(static_cast<int>(std::lround(s.chance * 100.0f))) + "%";
    valueRow(chanceBar(), "Chance", chance, s.chance, true);

    renderer.drawText("Ratchet", NUIPoint(x, ratchetButton(1).y - 18.0f), 12.0f, secondary);
    for (int hits = 1; hits <= 4; ++hits) {
        const NUIRect r = ratchetButton(hits);
        const bool active = any && s.ratchet == hits;
        renderer.fillRect(r, active ? raised : bed);
        const std::string label = "\xC3\x97" + std::to_string(hits);
        const float lw = renderer.measureText(label, 12.0f).width;
        renderer.drawText(label, NUIPoint(r.x + (r.width - lw) * 0.5f, renderer.calculateTextY(r, 12.0f)), 12.0f,
                          active ? primary : (any ? secondary : muted));
    }

    // Muse, under the selection (board A + C): the next bar's ideas, the previewed one lit; what Muse listens to.
    {
        const auto violet = NUIColor::fromHex(0xa78bfa);
        const float ruleY = ratchetButton(1).bottom() + 16.0f;
        renderer.drawLine(NUIPoint(x, ruleY), NUIPoint(b.right() - kPad, ruleY), 1.0f, theme.getColor("border"));
        const float titleY = ruleY + 12.0f;
        renderer.drawText("MUSE \xC2\xB7 NEXT BAR", NUIPoint(x, titleY), 11.0f, museMode_ == MuseMode::Off ? muted : violet);
        const int count = ideas_ ? static_cast<int>(ideas_->size()) : 0;
        if (museMode_ == MuseMode::Off) {
            renderer.drawText("Muse is off.", NUIPoint(x, titleY + 30.0f), 12.0f, muted);
        } else if (count == 0) {
            renderer.drawText("Listening. An idea shows as a dot", NUIPoint(x, titleY + 30.0f), 12.0f, muted);
            renderer.drawText("on the ruler when you pause.", NUIPoint(x, titleY + 48.0f), 12.0f, muted);
        } else {
            const std::string n = std::to_string(count) + (count == 1 ? " idea" : " ideas");
            renderer.drawText(n, NUIPoint(b.right() - kPad - renderer.measureText(n, 12.0f).width, titleY), 12.0f,
                              theme.getColor("textDisabled"));
            for (int i = 0; i < count; ++i) {
                const NUIRect r = ideaButton(i);
                const bool on = i == previewed_;
                renderer.fillRect(r, on ? NUIColor::fromHex(0x1c1530) : bed);
                renderer.strokeRect(r, 1.0f, on ? NUIColor::fromHex(0x7c3aed) : theme.getColor("border"));
                std::string label = std::to_string(i + 1) + " \xC2\xB7 " + (*ideas_)[static_cast<size_t>(i)].label;
                while (label.size() > 4 && renderer.measureText(label, 12.0f).width > r.width - 16.0f) label.pop_back();
                renderer.drawText(label, NUIPoint(r.x + 8.0f, renderer.calculateTextY(r, 12.0f)), 12.0f, on ? primary : secondary);
            }
            float y = ideaButton(count - 1).bottom() + 12.0f;
            if (!listeningTo_.empty()) {
                renderer.drawText("Listening to: " + listeningTo_, NUIPoint(x, y), 12.0f, muted);
                y += 20.0f;
            }
            renderer.drawText(previewed_ < 0 ? "Press Tab to see the first" : "Tab keeps it \xC2\xB7 \xE2\x87\xA7Tab next \xC2\xB7 Esc",
                              NUIPoint(x, y), 12.0f, muted);
        }
    }

    // The legend, at the foot (board A + C).
    renderer.drawText("Striped = chance \xC2\xB7 ticks = ratchet", NUIPoint(x, b.bottom() - kPad - 36.0f), 12.0f, muted);
    renderer.drawText("Outlines = other units \xC2\xB7 dashed = Muse", NUIPoint(x, b.bottom() - kPad - 18.0f), 12.0f, muted);
}

} // namespace AestraUI
