// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-AR1 (FD-52): the Arsenal's pattern menu. Three undoable edits to a pattern's identity, one command each:
// Rename, Duplicate, Delete. None of them touches notes, so none can disturb playback beyond the pattern list.

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"

#include <algorithm>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/// Rename a pattern. An empty or unchanged name is a no-op that records nothing (execute() leaves it unexecuted).
class RenamePatternCommand final : public ICommand {
public:
    RenamePatternCommand(PatternManager& patterns, PatternID id, std::string newName)
        : m_patterns(patterns), m_id(id), m_new(std::move(newName)) {}

    void execute() override {
        if (m_executed) return;
        auto* p = m_patterns.getPattern(m_id);
        if (!p || m_new.empty() || p->name == m_new) return;
        m_old = p->name;
        p->name = m_new;
        m_executed = true;
    }
    void undo() override {
        if (!m_executed) return;
        if (auto* p = m_patterns.getPattern(m_id)) p->name = m_old;
        m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Rename Pattern"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_old.size() + m_new.size(); }
    bool changesProjectState() const override { return true; }
    bool wasApplied() const { return m_executed; }

private:
    PatternManager& m_patterns;
    PatternID m_id;
    std::string m_old, m_new;
    bool m_executed = false;
};

/// Copy a pattern under a name nothing else uses ("Verse copy", "Verse copy 2", ...). Undo takes the copy out and
/// redo puts the same copy (same id) back, so anything that remembered the id still finds it.
class DuplicatePatternCommand final : public ICommand {
public:
    DuplicatePatternCommand(PatternManager& patterns, PatternID sourceId) : m_patterns(patterns), m_source(sourceId) {}

    /// "<name> copy", then "<name> copy 2", ... the first one no pattern already has.
    static std::string copyName(const PatternManager& patterns, const std::string& base) {
        auto taken = [&](const std::string& candidate) {
            bool found = false;
            patterns.forEachPattern([&](const PatternSource& p) { found = found || p.name == candidate; });
            return found;
        };
        const std::string stem = (base.empty() ? std::string("Pattern") : base) + " copy";
        if (!taken(stem)) return stem;
        for (int n = 2;; ++n) {
            const std::string candidate = stem + " " + std::to_string(n);
            if (!taken(candidate)) return candidate;
        }
    }

    void execute() override {
        if (m_executed) return;
        if (m_detached) {
            if (!m_patterns.reinsertPattern(m_detached)) return;
        } else {
            const auto* src = m_patterns.getPattern(m_source);
            if (!src) return;
            const std::string name = copyName(m_patterns, src->name);
            m_copy = m_patterns.clonePattern(m_source);
            if (!m_copy.isValid()) return;
            if (auto* p = m_patterns.getPattern(m_copy)) p->name = name;
        }
        m_executed = true;
    }
    void undo() override {
        if (!m_executed) return;
        m_detached = m_patterns.detachPattern(m_copy);
        m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Duplicate Pattern"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    PatternID copyId() const { return m_copy; }

private:
    PatternManager& m_patterns;
    PatternID m_source;
    PatternID m_copy;
    std::unique_ptr<PatternSource> m_detached;
    bool m_executed = false;
};

/// Delete a pattern, keeping it (notes, swing, routing, id) so undo puts back exactly what was there. The caller
/// decides whether deleting is allowed (a pattern placed on the timeline is not); the command only does it.
class DeletePatternCommand final : public ICommand {
public:
    DeletePatternCommand(PatternManager& patterns, PatternID id) : m_patterns(patterns), m_id(id) {}

    void execute() override {
        if (m_executed) return;
        m_detached = m_patterns.detachPattern(m_id);
        m_executed = m_detached != nullptr;
    }
    void undo() override {
        if (!m_executed) return;
        if (m_patterns.reinsertPattern(m_detached)) m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Delete Pattern"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patterns;
    PatternID m_id;
    std::unique_ptr<PatternSource> m_detached;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
