// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MoveClipCommand Unit Tests
// Tests: execute, undo, redo, state capture, edge cases

#include "Commands/MoveClipCommand.h"

#include "Commands/CommandHistory.h"
#include "Models/PlaylistModel.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

// =============================================================================
// TEST FIXTURE
// =============================================================================

class MoveClipTestFixture {
public:
    MoveClipTestFixture() {
        // Create a lane
        m_laneId = m_model.createLane("Test Lane");

        // Add a clip at beat 10
        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.patternId = PatternID(1); // Simple ID
        clip.startBeat = 10.0;
        clip.durationBeats = 4.0;

        m_model.addClip(m_laneId, clip);
        m_clipId = clip.id;
    }

    PlaylistModel& getModel() { return m_model; }
    ClipInstanceID getClipId() const { return m_clipId; }
    PlaylistLaneID getLaneId() const { return m_laneId; }

    double getClipBeat() {
        auto* clip = m_model.getClip(m_clipId);
        return clip ? clip->startBeat : -1.0;
    }

    PlaylistLaneID getClipLane() { return m_model.findClipLane(m_clipId); }

private:
    PlaylistModel m_model;
    ClipInstanceID m_clipId;
    PlaylistLaneID m_laneId;
};

// =============================================================================
// TEST FUNCTIONS
// =============================================================================

bool testExecuteMovesClip() {
    std::cout << "TEST: execute moves clip... ";

    MoveClipTestFixture fixture;

    // Verify initial position
    assert(fixture.getClipBeat() == 10.0);

    // Create move command to beat 20
    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    cmd->execute();

    // Verify new position
    assert(fixture.getClipBeat() == 20.0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testUndoRestoresOriginalPosition() {
    std::cout << "TEST: undo restores original position... ";

    MoveClipTestFixture fixture;

    // Move to 20
    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    cmd->execute();
    assert(fixture.getClipBeat() == 20.0);

    // Undo
    cmd->undo();

    // Verify restored
    assert(fixture.getClipBeat() == 10.0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testRedoReappliesMove() {
    std::cout << "TEST: redo reapplies move... ";

    MoveClipTestFixture fixture;

    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    cmd->execute();
    cmd->undo();
    assert(fixture.getClipBeat() == 10.0);

    cmd->redo();

    assert(fixture.getClipBeat() == 20.0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testDoubleExecuteIsNoOp() {
    std::cout << "TEST: double execute is no-op... ";

    MoveClipTestFixture fixture;

    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    cmd->execute();
    cmd->execute(); // Should be no-op

    assert(fixture.getClipBeat() == 20.0); // Still at 20

    std::cout << "✅ PASS\n";
    return true;
}

bool testUndoBeforeExecuteIsNoOp() {
    std::cout << "TEST: undo before execute is no-op... ";

    MoveClipTestFixture fixture;

    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    cmd->undo(); // Should be no-op

    assert(fixture.getClipBeat() == 10.0); // Still at original

    std::cout << "✅ PASS\n";
    return true;
}

bool testMoveToDifferentLane() {
    std::cout << "TEST: move to different lane... ";

    MoveClipTestFixture fixture;

    // Create second lane
    PlaylistLaneID lane2 = fixture.getModel().createLane("Lane 2");

    assert(fixture.getClipLane() == fixture.getLaneId());

    // Move to lane 2
    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 10.0, lane2);

    cmd->execute();

    assert(fixture.getClipLane() == lane2);

    // Undo should restore original lane
    cmd->undo();

    assert(fixture.getClipLane() == fixture.getLaneId());

    std::cout << "✅ PASS\n";
    return true;
}

bool testMoveWithBeatAndLaneChange() {
    std::cout << "TEST: move with beat and lane change... ";

    MoveClipTestFixture fixture;

    PlaylistLaneID lane2 = fixture.getModel().createLane("Lane 2");

    // Move to beat 25, lane 2
    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 25.0, lane2);

    cmd->execute();

    assert(fixture.getClipBeat() == 25.0);
    assert(fixture.getClipLane() == lane2);

    // Undo restores both
    cmd->undo();

    assert(fixture.getClipBeat() == 10.0);
    assert(fixture.getClipLane() == fixture.getLaneId());

    std::cout << "✅ PASS\n";
    return true;
}

bool testGetName() {
    std::cout << "TEST: getName... ";

    MoveClipTestFixture fixture;

    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    assert(cmd->getName() == "Move Clip");

    std::cout << "✅ PASS\n";
    return true;
}

bool testChangesProjectState() {
    std::cout << "TEST: changesProjectState... ";

    MoveClipTestFixture fixture;

    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    assert(cmd->changesProjectState() == true);

    std::cout << "✅ PASS\n";
    return true;
}

bool testGetSizeInBytes() {
    std::cout << "TEST: getSizeInBytes... ";

    MoveClipTestFixture fixture;

    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 20.0, PlaylistLaneID());

    size_t size = cmd->getSizeInBytes();
    assert(size > 0);
    assert(size == sizeof(MoveClipCommand));

    std::cout << "✅ PASS\n";
    return true;
}

bool testInvalidClipId() {
    std::cout << "TEST: invalid clip ID handling... ";

    MoveClipTestFixture fixture;

    // Create command with non-existent clip
    ClipInstanceID fakeId = ClipInstanceID::fromString("fake-id");
    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fakeId, 20.0, PlaylistLaneID());

    // Should not crash, should be no-op
    cmd->execute();
    cmd->undo();
    cmd->redo();

    std::cout << "✅ PASS\n";
    return true;
}

// =============================================================================
// INTEGRATION: CommandHistory + MoveClipCommand
// =============================================================================

bool testHistoryIntegration() {
    std::cout << "TEST: CommandHistory + MoveClipCommand integration... ";

    MoveClipTestFixture fixture;
    CommandHistory history;

    // Initial position
    assert(fixture.getClipBeat() == 10.0);

    // Move via history
    auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), 30.0, PlaylistLaneID());

    history.pushAndExecute(cmd);

    assert(fixture.getClipBeat() == 30.0);
    assert(history.canUndo() == true);
    assert(history.getUndoName() == "Move Clip");

    // Undo via history
    history.undo();
    assert(fixture.getClipBeat() == 10.0);
    assert(history.canRedo() == true);
    assert(history.getRedoName() == "Move Clip");

    // Redo via history
    history.redo();
    assert(fixture.getClipBeat() == 30.0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testMultipleMoves() {
    std::cout << "TEST: multiple moves in history... ";

    MoveClipTestFixture fixture;
    CommandHistory history;

    // Move 10 → 20 → 30 → 40
    for (double beat = 20.0; beat <= 40.0; beat += 10.0) {
        auto cmd = std::make_shared<MoveClipCommand>(fixture.getModel(), fixture.getClipId(), beat, PlaylistLaneID());
        history.pushAndExecute(cmd);
    }

    assert(fixture.getClipBeat() == 40.0);

    // Undo back to start
    history.undo(); // 40 → 30
    history.undo(); // 30 → 20
    history.undo(); // 20 → 10

    assert(fixture.getClipBeat() == 10.0);

    std::cout << "✅ PASS\n";
    return true;
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    std::cout << "=================================\n";
    std::cout << "  MoveClipCommand Unit Tests\n";
    std::cout << "=================================\n\n";

    int passed = 0;
    int failed = 0;

    struct Test {
        const char* name;
        bool (*func)();
    };

    Test tests[] = {
        {"Execute moves clip", testExecuteMovesClip},
        {"Undo restores original position", testUndoRestoresOriginalPosition},
        {"Redo reapplies move", testRedoReappliesMove},
        {"Double execute is no-op", testDoubleExecuteIsNoOp},
        {"Undo before execute is no-op", testUndoBeforeExecuteIsNoOp},
        {"Move to different lane", testMoveToDifferentLane},
        {"Move with beat and lane change", testMoveWithBeatAndLaneChange},
        {"Get name", testGetName},
        {"Changes project state", testChangesProjectState},
        {"Get size in bytes", testGetSizeInBytes},
        {"Invalid clip ID handling", testInvalidClipId},
        {"History integration", testHistoryIntegration},
        {"Multiple moves in history", testMultipleMoves},
    };

    for (const auto& test : tests) {
        try {
            if (test.func()) {
                passed++;
            } else {
                failed++;
                std::cout << "❌ FAIL: " << test.name << "\n";
            }
        } catch (const std::exception& e) {
            failed++;
            std::cout << "❌ EXCEPTION in " << test.name << ": " << e.what() << "\n";
        }
    }

    std::cout << "\n=================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "=================================\n";

    return failed > 0 ? 1 : 0;
}
