// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/AddChannelCommand.h"
#include "Commands/ICommand.h"
#include "Commands/RoutingCommands.h"
#include "Commands/SetUnitMixerChannelCommand.h"
#include "Models/RoleBus.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Make the bus a RoleBusPlan describes, as one undoable step: add the channel, route the members into it.
 *
 * The user says "a drum bus"; this does the mechanical part (RoleBus.h). It is the same channel, routing and unit
 * commands a user would use by hand, so the result is an ordinary bus: members can be added or removed, and every
 * routing control works on it. A member whose route would be illegal (a loop) is left out, not an error.
 */
class CreateRoleBusCommand : public ICommand {
public:
    CreateRoleBusCommand(TrackManager& manager, RoleBusPlan plan) : m_manager(manager), m_plan(std::move(plan)) {}

    void execute() override {
        if (m_executed || !m_plan.worthOffering()) return;
        auto add = std::make_shared<AddChannelCommand>(m_manager, m_plan.name);
        add->execute();
        const uint32_t bus = add->getCreatedChannelId();
        if (bus == 0) return;
        m_steps.clear();
        m_steps.push_back(add);
        for (const auto& member : m_plan.members) {
            std::shared_ptr<ICommand> step;
            if (member.kind == RoleBusMember::Kind::UnitOnMaster) {
                step = std::make_shared<SetUnitMixerChannelCommand>(m_manager, member.unit, bus);
            } else {
                if (!m_manager.canRouteTo(member.channelId, bus)) continue; // a loop: not this one
                step = std::make_shared<SetMainOutputCommand>(m_manager, member.channelId, bus);
            }
            step->execute();
            m_steps.push_back(std::move(step));
        }
        m_busChannelId = bus;
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) (*it)->undo(); // the members first, the bus last
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        for (auto& step : m_steps) step->redo(); // the same channel comes back (AddChannelCommand keeps its channel)
        m_executed = !m_steps.empty();
    }

    uint32_t busChannelId() const { return m_executed ? m_busChannelId : 0; }
    size_t memberCount() const { return m_steps.empty() ? 0 : m_steps.size() - 1; }

    std::string getName() const override { return "Create " + m_plan.name; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_steps.size() * sizeof(void*) * 4; }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_manager;
    RoleBusPlan m_plan;
    std::vector<std::shared_ptr<ICommand>> m_steps; ///< the add first, then each member's route
    uint32_t m_busChannelId{0};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
