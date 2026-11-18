// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ArsenalSceneMenu.h"

#include "Commands/PatternMenuCommands.h"
#include "Commands/CommandTransaction.h"
#include "Models/TrackManager.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "NUIComponent.h"
#include "NUIContextMenu.h"
#include "NUIRenderer.h"
#include "NUITextInput.h"

#include <algorithm>

namespace Aestra {

namespace {
AestraUI::NUIComponent* rootOf(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) root = root->getParent();
    return root;
}

void detach(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (menu)
        if (auto* parent = menu->getParent()) parent->removeChild(menu);
}
} // namespace

ArsenalSceneMenu::~ArsenalSceneMenu() { cancel(); }

void ArsenalSceneMenu::draw(AestraUI::NUIRenderer& renderer) {
    if (m_input) m_input->onRender(renderer);
}

bool ArsenalSceneMenu::mouse(const AestraUI::NUIMouseEvent& event) {
    if (!m_input) return false;
    if (m_input->getBounds().contains(event.position)) return m_input->onMouseEvent(event);
    if (event.pressed) finishRename(true);
    return false;
}

bool ArsenalSceneMenu::key(const AestraUI::NUIKeyEvent& event) {
    if (!m_input) return false;
    m_input->onKeyEvent(event);
    return true;
}

void ArsenalSceneMenu::cancel() {
    detach(m_menu);
    m_menu.reset();
    finishRename(false);
    m_retired.reset();
}

void ArsenalSceneMenu::show(const Request& request) {
    if (!request.tracks || !request.owner || request.id == 0) return;
    cancel();
    const Audio::PatternID id{request.id};
    auto& tracks = *request.tracks;
    std::vector<uint64_t> selected = request.selectedIds;
    if (selected.empty() || std::find(selected.begin(), selected.end(), request.id) == selected.end())
        selected = {request.id};
    std::sort(selected.begin(), selected.end(), [&](uint64_t a, uint64_t b) {
        const auto indexOf = [&](uint64_t value) {
            const auto it = std::find_if(request.scenes.begin(), request.scenes.end(),
                [value](const auto& scene) { return scene.id == value; });
            return std::distance(request.scenes.begin(), it);
        };
        return indexOf(a) < indexOf(b);
    });
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    m_menu = std::make_shared<AestraUI::NUIContextMenu>();
    m_menu->setOnHide([this]() { detach(m_menu); });

    auto rename = std::make_shared<AestraUI::NUIContextMenuItem>("Rename");
    rename->setEnabled(selected.size() == 1);
    rename->setOnClick([this, request]() { beginRename(request); });
    m_menu->addItem(rename);

    m_menu->addItem("Duplicate", [request, selected, id]() {
        auto& patterns = request.tracks->getPatternManager();
        auto transaction = std::make_shared<Audio::CommandTransaction>("Duplicate Arsenal Pattern(s)");
        std::vector<std::shared_ptr<Audio::DuplicatePatternCommand>> commands;
        std::vector<uint64_t> copies;
        uint64_t activeCopy = 0;
        for (const uint64_t source : selected) {
            const auto* pattern = patterns.getPattern(Audio::PatternID{source});
            if (!pattern || !pattern->isMidi()) continue;
            auto copy = std::make_shared<Audio::DuplicatePatternCommand>(patterns, Audio::PatternID{source});
            copy->execute();
            if (!copy->copyId().isValid()) continue;
            if (source == id.value) activeCopy = copy->copyId().value;
            copies.push_back(copy->copyId().value);
            transaction->add(copy);
            commands.push_back(std::move(copy));
        }
        if (commands.empty()) return;
        transaction->markExecuted();
        if (!request.tracks->getCommandHistory().pushExecuted(transaction)) {
            for (auto it = commands.rbegin(); it != commands.rend(); ++it) (*it)->undo();
            return;
        }
        if (request.changed) request.changed(activeCopy, copies);
    });

    auto& patterns = tracks.getPatternManager();
    const auto allPatterns = patterns.getAllPatterns();
    const size_t midiCount = static_cast<size_t>(std::count_if(allPatterns.begin(), allPatterns.end(),
        [](const auto& pattern) { return pattern && pattern->isMidi(); }));
    bool allowed = true;
    std::string why;
    for (const uint64_t patternId : selected) {
        const Audio::PatternID target{patternId};
        const auto* pattern = patterns.getPattern(target);
        if (!pattern || !pattern->isMidi()) {
            allowed = false;
            why = "Delete (selection contains an unavailable pattern)";
            break;
        }
        if (tracks.getPlaylistModel().isPatternUsed(target)) {
            allowed = false;
            why = "Delete (a selected pattern plays in the timeline)";
            break;
        }
    }
    if (allowed && (midiCount <= selected.size())) {
        allowed = false;
        why = midiCount <= 1 ? "Delete (the only pattern)" : "Delete (Arsenal must keep one pattern)";
    }
    auto del = std::make_shared<AestraUI::NUIContextMenuItem>(allowed ? "Delete" : why);
    del->setDestructive(true);
    del->setEnabled(allowed);
    del->setOnClick([request, selected]() {
        auto& patterns = request.tracks->getPatternManager();
        auto transaction = std::make_shared<Audio::CommandTransaction>("Delete Arsenal Pattern(s)");
        std::vector<std::shared_ptr<Audio::DeletePatternCommand>> commands;
        for (const uint64_t patternId : selected) {
            auto remove = std::make_shared<Audio::DeletePatternCommand>(patterns, Audio::PatternID{patternId});
            remove->execute();
            if (!remove->isUndoable()) {
                for (auto it = commands.rbegin(); it != commands.rend(); ++it) (*it)->undo();
                return;
            }
            transaction->add(remove);
            commands.push_back(std::move(remove));
        }
        if (commands.empty()) return;

        uint64_t next = request.activeId;
        if (std::find(selected.begin(), selected.end(), next) != selected.end()) {
            const auto active = std::find_if(request.scenes.begin(), request.scenes.end(),
                [next](const auto& scene) { return scene.id == next; });
            next = 0;
            if (active != request.scenes.end()) {
                for (auto it = std::next(active); it != request.scenes.end(); ++it)
                    if (std::find(selected.begin(), selected.end(), it->id) == selected.end()) { next = it->id; break; }
                if (next == 0) {
                    auto it = active;
                    while (it != request.scenes.begin()) {
                        --it;
                        if (std::find(selected.begin(), selected.end(), it->id) == selected.end()) { next = it->id; break; }
                    }
                }
            }
        }

        transaction->markExecuted();
        if (!request.tracks->getCommandHistory().pushExecuted(transaction)) {
            for (auto it = commands.rbegin(); it != commands.rend(); ++it) (*it)->undo();
            return;
        }
        const std::vector<uint64_t> selection = next == 0 ? std::vector<uint64_t>{} : std::vector<uint64_t>{next};
        if (request.changed) request.changed(next, selection);
    });
    m_menu->addItem(del);

    AestraUI::NUIComponent* root = rootOf(request.owner);
    if (!root) root = request.owner;
    root->addChild(m_menu);
    m_menu->setOwner(request.owner->weak_from_this()); // the menu outlives the panel's callbacks, so it drops them with it
    m_menu->showAt(request.at);
    root->repaint();
}

void ArsenalSceneMenu::beginRename(const Request& request) {
    const auto* pattern = request.tracks->getPatternManager().getPattern(Audio::PatternID{request.id});
    if (!pattern || request.chip.width <= 0.0f) return;
    finishRename(false);
    m_retired.reset();

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    m_input = std::make_shared<AestraUI::NUITextInput>(pattern->name);
    m_input->setBounds(request.chip);
    m_input->setTextColor(theme.getColor("textPrimary"));
    m_input->setBackgroundColor(theme.getColor("inputBgDefault"));
    m_input->setBorderColor(theme.getColor("inputBorderFocus"));
    m_input->setBorderWidth(1.0f);
    m_input->setBorderRadius(3.0f);
    m_input->setFocusedBorderColor(theme.getColor("accentPrimary"));
    m_input->setOnReturnKey([this]() { finishRename(true); });
    m_input->setOnEscapeKey([this]() { finishRename(false); });
    m_input->setOnFocusLost([this]() { finishRename(true); });

    m_inputOwner = request.owner;
    m_tracks = request.tracks;
    m_renameId = request.id;
    m_changed = request.changed;
    m_selectedIds = request.selectedIds;
    request.owner->addChild(m_input);
    m_input->setFocused(true);
    m_input->setCaretPosition(static_cast<int>(pattern->name.length()));
    m_input->selectAll();
    request.owner->repaint();
}

void ArsenalSceneMenu::finishRename(bool commit) {
    if (!m_input) return;
    // Take the box out first: committing (or losing focus) can arrive from inside the box's own handler.
    auto input = std::move(m_input);
    m_input.reset();
    const std::string text = input->getText();
    if (m_inputOwner) {
        m_inputOwner->removeChild(input);
        m_inputOwner->repaint();
    }
    m_retired = std::move(input);

    if (!commit || !m_tracks || text.empty()) return;
    const Audio::PatternID id{m_renameId};
    const auto* pattern = m_tracks->getPatternManager().getPattern(id);
    if (!pattern || pattern->name == text) return;
    auto rename = std::make_shared<Audio::RenamePatternCommand>(m_tracks->getPatternManager(), id, text);
    m_tracks->getCommandHistory().pushAndExecute(rename);
    if (m_changed) m_changed(0, m_selectedIds);
}

} // namespace Aestra
