// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Presets/PresetSession.h"

#include "Commands/ApplyPresetCommand.h"

#include <utility>

namespace Aestra {
namespace Audio {
namespace Presets {

PresetSession::PresetSession(std::shared_ptr<IPluginInstance> plugin, const PresetLibrary& library,
                             std::string trackName, Recorder record)
    : m_plugin(std::move(plugin)), m_library(library), m_trackName(std::move(trackName)), m_record(std::move(record)) {
    refresh();
}

void PresetSession::setProjectPresets(ProjectPresets list, SaveToProject save) {
    m_projectList = std::move(list);
    m_projectSave = std::move(save);
    refresh();
}

void PresetSession::refresh() {
    if (!m_plugin) return;
    const std::string& id = m_plugin->getInfo().id;
    std::vector<Preset> all = m_library.list(id);
    if (m_projectList) {
        for (auto p : m_projectList(id)) {
            p.source = PresetSource::Project;
            all.push_back(std::move(p));
        }
    }
    m_presets = orderForTrack(std::move(all), m_trackName);
}

std::string PresetSession::currentName() const { return *m_current ? (*m_current)->name : std::string(); }

int PresetSession::changes() const {
    if (!m_plugin || !*m_current) return 0;
    return countChanges(*m_plugin, **m_current);
}

Preset PresetSession::snapshot() const { return capture(*m_plugin, "snapshot"); }

void PresetSession::notify() {
    if (*m_notifyCell) (*m_notifyCell)();
}

void PresetSession::record(Preset before, std::optional<Preset> beforeCurrent, std::optional<Preset> afterCurrent,
                           const std::string& label) {
    if (!m_record) return;
    std::weak_ptr<std::optional<Preset>> cell = m_current;
    std::weak_ptr<std::function<void()>> notifyCell = m_notifyCell;
    auto onApplied = [cell, notifyCell, beforeCurrent, afterCurrent](bool toAfter) {
        if (auto c = cell.lock()) *c = toAfter ? afterCurrent : beforeCurrent;
        if (auto n = notifyCell.lock(); n && *n) (*n)();
    };
    m_record(std::make_shared<ApplyPresetCommand>(m_plugin, std::move(before), snapshot(), label, onApplied));
}

bool PresetSession::pick(size_t index) {
    if (!m_plugin || index >= m_presets.size()) return false;
    if (browsing()) cancelBrowse();
    const Preset before = snapshot();
    const auto beforeCurrent = *m_current;
    if (!apply(*m_plugin, m_presets[index])) return false;
    *m_current = m_presets[index];
    record(before, beforeCurrent, *m_current, "Load Preset \"" + m_presets[index].name + "\"");
    notify();
    return true;
}

bool PresetSession::step(int direction) {
    if (m_presets.empty() || direction == 0) return false;
    const auto n = static_cast<long>(m_presets.size());
    long at = -1;
    if (*m_current)
        for (long i = 0; i < n; ++i)
            if (m_presets[i].name == (*m_current)->name && m_presets[i].source == (*m_current)->source) at = i;
    long next;
    if (at < 0) next = direction > 0 ? 0 : n - 1;
    else next = ((at + (direction > 0 ? 1 : -1)) % n + n) % n;
    return pick(static_cast<size_t>(next));
}

void PresetSession::beginBrowse() {
    if (!m_plugin || browsing()) return;
    m_browseBefore = snapshot();
    m_browseFrom = *m_current;
    m_auditioned.reset();
}

bool PresetSession::audition(size_t index) {
    if (!m_plugin || index >= m_presets.size()) return false;
    if (!browsing()) beginBrowse();
    if (!apply(*m_plugin, m_presets[index])) return false;
    m_auditioned = index;
    notify();
    return true;
}

bool PresetSession::keepBrowse() {
    if (!browsing()) return false;
    Preset before = std::move(*m_browseBefore);
    const auto beforeCurrent = m_browseFrom;
    m_browseBefore.reset();
    m_browseFrom.reset();
    if (!m_auditioned) return false; // opened and closed without hearing anything: nothing changed
    *m_current = m_presets[*m_auditioned];
    m_auditioned.reset();
    record(std::move(before), beforeCurrent, *m_current, "Load Preset \"" + (*m_current)->name + "\"");
    notify();
    return true;
}

void PresetSession::cancelBrowse() {
    if (!browsing()) return;
    if (m_auditioned) apply(*m_plugin, *m_browseBefore);
    *m_current = m_browseFrom;
    m_browseBefore.reset();
    m_browseFrom.reset();
    m_auditioned.reset();
    notify();
}

bool PresetSession::beginCompare() {
    if (!m_plugin || comparing()) return false;
    if (browsing()) cancelBrowse();
    Compare c;
    c.a = snapshot();
    c.slot[0] = c.a;
    c.slot[1] = c.a;
    c.from[0] = *m_current;
    c.from[1] = *m_current;
    m_compare = std::move(c);
    return true;
}

void PresetSession::flip() {
    if (!comparing()) return;
    Compare& c = *m_compare;
    c.slot[c.active] = snapshot();
    c.from[c.active] = *m_current;
    c.active = 1 - c.active;
    apply(*m_plugin, c.slot[c.active]);
    *m_current = c.from[c.active];
    notify();
}

bool PresetSession::keepActive() {
    if (!comparing()) return false;
    Compare c = std::move(*m_compare);
    m_compare.reset();
    if (countChanges(*m_plugin, c.a) == 0) return false; // kept A unchanged: nothing to undo
    record(std::move(c.a), c.from[0], *m_current, c.active == 0 ? "Keep A" : "Keep B");
    notify();
    return true;
}

void PresetSession::endCompare() {
    if (!comparing()) return;
    Compare c = std::move(*m_compare);
    m_compare.reset();
    apply(*m_plugin, c.a);
    *m_current = c.from[0];
    notify();
}

std::string PresetSession::suggestedName() const {
    std::vector<std::string> taken;
    for (const auto& p : m_presets)
        if (p.source != PresetSource::Factory) taken.push_back(p.name);
    if (canUpdateCurrent()) return (*m_current)->name; // Update keeps the name
    // With no preset to name it after, the plugin says what it is: "Lead Vox" + "Compressor".
    std::string from = currentName();
    if (from.empty() && m_plugin) {
        from = m_plugin->getInfo().name;
        const std::string vendor = m_plugin->getInfo().vendor;
        if (from.rfind("Aestra ", 0) == 0) from = from.substr(7);
        else if (!vendor.empty() && from.rfind(vendor + " ", 0) == 0) from = from.substr(vendor.size() + 1);
    }
    return suggestName(m_trackName, from, taken);
}

bool PresetSession::canUpdateCurrent() const {
    return *m_current && (*m_current)->source != PresetSource::Factory;
}

std::optional<Preset> PresetSession::save(const std::string& name, Place place, bool update, std::string* error) {
    if (!m_plugin) return std::nullopt;
    Preset p = capture(*m_plugin, name);
    if (p.name.empty()) {
        if (error) *error = "A preset needs a name.";
        return std::nullopt;
    }
    if (*m_current) {
        // A new preset made from another keeps where it belongs in the browser and what it was tagged.
        p.category = (*m_current)->category;
        p.tags = (*m_current)->tags;
    }
    std::optional<Preset> saved;
    if (place == Place::Project) {
        if (!m_projectSave) {
            if (error) *error = "This project cannot keep presets.";
            return std::nullopt;
        }
        p.source = PresetSource::Project;
        if (!m_projectSave(p)) {
            if (error) *error = "The project could not keep the preset.";
            return std::nullopt;
        }
        saved = p;
    } else {
        saved = m_library.saveUser(p, update, error);
        if (!saved) return std::nullopt;
    }
    *m_current = *saved;
    refresh();
    notify();
    return saved;
}

} // namespace Presets
} // namespace Audio
} // namespace Aestra
