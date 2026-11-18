// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "Models/MusicalRole.h"
#include "NUITextInput.h"
#include "NUIContextMenu.h"
#include "UnitManager.h"
#include <functional>

namespace AestraUI {

/**
 * @brief Editable unit name label used inside Arsenal UnitRow.
 *
 * Renders the unit name + type label. Double-click renames the unit.
 * Right-click shows a rename menu. Inline editing swaps in a NUITextInput.
 */
class UnitNameLabel : public NUIComponent {
public:
    explicit UnitNameLabel(const std::string& name, Aestra::Audio::UnitType type);
    /** @brief Compact representation: name only, no type line (responsive density). */
    void setCompact(bool compact) { m_compact = compact; repaint(); }

    void setUnitName(const std::string& name);
    void setUnitType(Aestra::Audio::UnitType type);
    /** @brief What the sound is in the song, shown beside its type: dim when read from the name, brighter when the user chose it. */
    void setRole(const Aestra::Audio::MusicalRole& role) { if (role != m_role) { m_role = role; repaint(); } }
    /** @brief True while an inline rename is active (row key handling must not steal Delete/Backspace). */
    bool isRenaming() const { return m_isRenaming; }

    /** @brief Callback fired when the separate editor hit area is activated. */
    std::function<void()> m_onOpenEditor;
    /** @brief Callback fired when the name is committed after a rename. */
    std::function<void(const std::string&)> m_onRename;

    /** @brief Begin inline rename (can be triggered externally, e.g. from row context menu). */
    void beginRename();

public:
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

private:
    void commitRename();
    void cancelRename();

    std::string m_unitName;
    Aestra::Audio::UnitType m_unitType;
    Aestra::Audio::MusicalRole m_role;
    bool m_isRenaming = false;
    bool m_compact = false;
    long long m_lastClickTimeMs = 0;

    std::shared_ptr<NUITextInput> m_textInput;
};

} // namespace AestraUI
