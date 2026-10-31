// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ArsenalSceneMenu.h"

#include "Commands/PatternMenuCommands.h"
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

    m_menu = std::make_shared<AestraUI::NUIContextMenu>();
    m_menu->setOnHide([this]() { detach(m_menu); });

    m_menu->addItem("Rename", [this, request]() { beginRename(request); });

    m_menu->addItem("Duplicate", [request, id]() {
        auto copy = std::make_shared<Audio::DuplicatePatternCommand>(request.tracks->getPatternManager(), id);
        request.tracks->getCommandHistory().pushAndExecute(copy);
        if (request.changed) request.changed(copy->copyId().value);
    });

    std::string why;
    const bool allowed = canDelete(request.tracks->getPlaylistModel().isPatternUsed(id), request.scenes.size(), &why);
    auto del = std::make_shared<AestraUI::NUIContextMenuItem>(allowed ? "Delete" : why);
    del->setDestructive(true);
    del->setEnabled(allowed);
    del->setOnClick([request, id]() {
        // Pick the neighbour before the pattern is gone; only move the selection when the active one is the one deleted.
        const uint64_t next = request.activeId == request.id ? ArsenalSceneMenu::selectAfterDelete(request.scenes, request.id) : 0;
        auto remove = std::make_shared<Audio::DeletePatternCommand>(request.tracks->getPatternManager(), id);
        request.tracks->getCommandHistory().pushAndExecute(remove);
        if (request.changed) request.changed(next);
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
    if (m_changed) m_changed(0);
}

} // namespace Aestra
