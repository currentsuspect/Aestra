// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CommandHistory Unit Tests
// Tests: pushAndExecute, undo, redo, canUndo/Redo, getUndo/RedoName, clear, limits

#include "Commands/CommandHistory.h"

#include "Commands/ICommand.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

// Test command that tracks execution state
class TestCommand : public ICommand {
public:
    TestCommand(const std::string& name, int* executeCount, int* undoCount)
        : m_name(name), m_executeCount(executeCount), m_undoCount(undoCount), m_executed(false) {}

    void execute() override {
        (*m_executeCount)++;
        m_executed = true;
    }

    void undo() override {
        (*m_undoCount)++;
        m_executed = false;
    }

    void redo() override {
        (*m_executeCount)++;
        m_executed = true;
    }

    std::string getName() const override { return m_name; }
    bool isExecuted() const { return m_executed; }

private:
    std::string m_name;
    int* m_executeCount;
    int* m_undoCount;
    bool m_executed;
};

// Failing command for error handling tests
class FailingCommand : public ICommand {
public:
    FailingCommand(bool failOnExecute, bool failOnUndo) : m_failOnExecute(failOnExecute), m_failOnUndo(failOnUndo) {}

    void execute() override {
        if (m_failOnExecute) {
            throw std::runtime_error("Execute failed");
        }
    }

    void undo() override {
        if (m_failOnUndo) {
            throw std::runtime_error("Undo failed");
        }
    }

    void redo() override {
        // For testing, redo just calls execute
        execute();
    }

    std::string getName() const override { return "FailingCommand"; }

private:
    bool m_failOnExecute;
    bool m_failOnUndo;
};

// Counter for callback tests
int g_callbackCount = 0;

void resetCallbackCount() {
    g_callbackCount = 0;
}

void incrementCallback() {
    g_callbackCount++;
}

// =============================================================================
// TEST FUNCTIONS
// =============================================================================

bool testPushAndExecute() {
    std::cout << "TEST: pushAndExecute... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("Test", &executeCount, &undoCount);
    history.pushAndExecute(cmd);

    assert(executeCount == 1);
    assert(undoCount == 0);
    assert(history.canUndo() == true);
    assert(history.canRedo() == false);
    assert(history.getUndoName() == "Test");

    std::cout << "✅ PASS\n";
    return true;
}

bool testUndo() {
    std::cout << "TEST: undo... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("Test", &executeCount, &undoCount);
    history.pushAndExecute(cmd);

    bool result = history.undo();

    assert(result == true);
    assert(executeCount == 1);
    assert(undoCount == 1);
    assert(history.canUndo() == false);
    assert(history.canRedo() == true);
    assert(history.getRedoName() == "Test");

    std::cout << "✅ PASS\n";
    return true;
}

bool testRedo() {
    std::cout << "TEST: redo... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("Test", &executeCount, &undoCount);
    history.pushAndExecute(cmd);
    history.undo();

    bool result = history.redo();

    assert(result == true);
    assert(executeCount == 2); // Initial + redo
    assert(undoCount == 1);
    assert(history.canUndo() == true);
    assert(history.canRedo() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testUndoRedoSequence() {
    std::cout << "TEST: undo/redo sequence... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    // Execute 3 commands
    for (int i = 0; i < 3; ++i) {
        auto cmd = std::make_shared<TestCommand>("Cmd" + std::to_string(i), &executeCount, &undoCount);
        history.pushAndExecute(cmd);
    }

    assert(executeCount == 3);
    assert(history.canUndo() == true);

    // Undo all
    history.undo();
    history.undo();
    history.undo();

    assert(undoCount == 3);
    assert(history.canUndo() == false);
    assert(history.canRedo() == true);

    // Redo all
    history.redo();
    history.redo();
    history.redo();

    assert(executeCount == 6); // 3 initial + 3 redos
    assert(history.canRedo() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testClearRedoOnNewCommand() {
    std::cout << "TEST: clear redo on new command... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    // Execute and undo
    auto cmd1 = std::make_shared<TestCommand>("Cmd1", &executeCount, &undoCount);
    history.pushAndExecute(cmd1);
    history.undo();

    assert(history.canRedo() == true);

    // New command should clear redo stack
    auto cmd2 = std::make_shared<TestCommand>("Cmd2", &executeCount, &undoCount);
    history.pushAndExecute(cmd2);

    assert(history.canRedo() == false);
    assert(history.getUndoName() == "Cmd2");

    std::cout << "✅ PASS\n";
    return true;
}

bool testEmptyUndoRedo() {
    std::cout << "TEST: empty undo/redo... ";

    CommandHistory history;

    assert(history.canUndo() == false);
    assert(history.canRedo() == false);
    assert(history.getUndoName() == "");
    assert(history.getRedoName() == "");

    // Should return false gracefully
    assert(history.undo() == false);
    assert(history.redo() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testHistoryLimits() {
    std::cout << "TEST: history limits... ";

    CommandHistory history;
    history.setMaxHistorySize(3);

    int executeCount = 0;
    int undoCount = 0;

    // Add 5 commands (only last 3 should be kept)
    for (int i = 0; i < 5; ++i) {
        auto cmd = std::make_shared<TestCommand>("Cmd" + std::to_string(i), &executeCount, &undoCount);
        history.pushAndExecute(cmd);
    }

    // Should only be able to undo 3 times
    assert(history.undo() == true);
    assert(history.undo() == true);
    assert(history.undo() == true);
    assert(history.undo() == false); // 4th should fail

    std::cout << "✅ PASS\n";
    return true;
}

bool testClear() {
    std::cout << "TEST: clear... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("Test", &executeCount, &undoCount);
    history.pushAndExecute(cmd);
    history.undo();

    assert(history.canUndo() == false);
    assert(history.canRedo() == true);

    history.clear();

    assert(history.canUndo() == false);
    assert(history.canRedo() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testCallback() {
    std::cout << "TEST: state changed callback... ";

    CommandHistory history;
    resetCallbackCount();

    history.setOnStateChanged(incrementCallback);

    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("Test", &executeCount, &undoCount);
    history.pushAndExecute(cmd); // Should trigger callback

    assert(g_callbackCount == 1);

    history.undo(); // Should trigger callback
    assert(g_callbackCount == 2);

    history.redo(); // Should trigger callback
    assert(g_callbackCount == 3);

    std::cout << "✅ PASS\n";
    return true;
}

bool testExecuteFailure() {
    std::cout << "TEST: execute failure handling... ";

    CommandHistory history;

    // Command that fails on execute
    auto failCmd = std::make_shared<FailingCommand>(true, false);

    // Should not throw, should not add to history
    history.pushAndExecute(failCmd);

    assert(history.canUndo() == false);
    assert(history.canRedo() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testUndoFailure() {
    std::cout << "TEST: undo failure handling... ";

    CommandHistory history;

    // Command that fails on undo
    auto failCmd = std::make_shared<FailingCommand>(false, true);

    history.pushAndExecute(failCmd);
    assert(history.canUndo() == true);

    // Undo should fail gracefully
    bool result = history.undo();

    // Per implementation: returns false on failure
    assert(result == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testNullCommand() {
    std::cout << "TEST: null command handling... ";

    CommandHistory history;

    // Should not crash with null
    history.pushAndExecute(nullptr);

    assert(history.canUndo() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testMemoryLimit() {
    std::cout << "TEST: memory limit... ";

    CommandHistory history;
    // Set a tiny memory limit (1 byte) to force aggressive trimming.
    // The default getSizeInBytes() returns sizeof(ICommand) which is at least
    // a few bytes, so even a 1-byte limit causes every push to trim the oldest entry.
    history.setMaxHistoryMemory(1);

    int executeCount = 0;
    int undoCount = 0;

    // Push 10 commands - the very tight limit should cause aggressive trimming
    for (int i = 0; i < 10; ++i) {
        auto cmd = std::make_shared<TestCommand>("Cmd" + std::to_string(i), &executeCount, &undoCount);
        history.pushAndExecute(cmd);
    }

    // We should have fewer than 10 undos available due to memory limit
    int undoableCount = 0;
    while (history.canUndo()) {
        history.undo();
        undoableCount++;
    }

    // Should have been limited (less than 10 commands retained)
    assert(undoableCount < 10);

    std::cout << "✅ PASS\n";
    return true;
}

bool testGetHistoryMemoryUsage() {
    std::cout << "TEST: getHistoryMemoryUsage... ";

    CommandHistory history;
    int executeCount = 0;
    int undoCount = 0;

    // Initially no memory usage
    size_t initialUsage = history.getHistoryMemoryUsage();
    assert(initialUsage == 0);

    // After pushing commands, usage should grow
    auto cmd1 = std::make_shared<TestCommand>("Cmd1", &executeCount, &undoCount);
    history.pushAndExecute(cmd1);
    size_t afterOne = history.getHistoryMemoryUsage();
    assert(afterOne > 0);

    auto cmd2 = std::make_shared<TestCommand>("Cmd2", &executeCount, &undoCount);
    history.pushAndExecute(cmd2);
    size_t afterTwo = history.getHistoryMemoryUsage();
    assert(afterTwo >= afterOne);

    // After clear, usage should drop to zero
    history.clear();
    size_t afterClear = history.getHistoryMemoryUsage();
    assert(afterClear == 0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testMemoryLimitUnlimited() {
    std::cout << "TEST: memory limit zero means unlimited... ";

    CommandHistory history;
    history.setMaxHistoryMemory(0); // 0 = unlimited

    int executeCount = 0;
    int undoCount = 0;

    // Push many commands - none should be trimmed
    for (int i = 0; i < 50; ++i) {
        auto cmd = std::make_shared<TestCommand>("Cmd" + std::to_string(i), &executeCount, &undoCount);
        history.pushAndExecute(cmd);
    }

    int undoableCount = 0;
    while (history.canUndo()) {
        history.undo();
        undoableCount++;
    }

    // All 50 should be undoable (memory limit is off)
    assert(undoableCount == 50);

    std::cout << "✅ PASS\n";
    return true;
}

// =============================================================================
// MAIN
// =============================================================================


// =============================================================================
// REGRESSION TEST: Callback re-entry safety
// =============================================================================
// Verifies that the state-change callback can safely call back into
// CommandHistory (e.g., querying canUndo()/canRedo()) without deadlocking.
//
// Before the fix, cmd->undo()/cmd->redo() were called while holding
// m_mutex. If a command's undo/redo triggered a callback that re-entered
// CommandHistory, it would deadlock on the non-recursive std::mutex.
//
// After the fix, command execution happens outside the lock, and
// state-change callbacks are always invoked outside the lock.

bool testCallbackCanQueryHistory() {
    std::cout << "TEST: callback can query history state (re-entry safety)... ";

    CommandHistory history;
    int queryCount = 0;

    // Register a callback that re-enters CommandHistory by querying state.
    // This would deadlock if callbacks were invoked while m_mutex is held.
    history.setOnStateChanged([&]() {
        queryCount++;
        // These all acquire m_mutex internally — safe only if caller doesn't hold it
        [[maybe_unused]] bool cu = history.canUndo();
        [[maybe_unused]] bool cr = history.canRedo();
        [[maybe_unused]] std::string un = history.getUndoName();
        [[maybe_unused]] std::string rn = history.getRedoName();
    });

    int executeCount = 0;
    int undoCount = 0;

    // pushAndExecute triggers callback → callback queries history
    auto cmd1 = std::make_shared<TestCommand>("Cmd1", &executeCount, &undoCount);
    history.pushAndExecute(cmd1);
    assert(queryCount == 1);

    // undo triggers callback → callback queries history
    history.undo();
    assert(queryCount == 2);

    // redo triggers callback → callback queries history
    history.redo();
    assert(queryCount == 3);

    // pushAndExecute a second command
    auto cmd2 = std::make_shared<TestCommand>("Cmd2", &executeCount, &undoCount);
    history.pushAndExecute(cmd2);
    assert(queryCount == 4);

    // clear triggers callback → callback queries history
    history.clear();
    assert(queryCount == 5);

    std::cout << "✅ PASS\n";
    return true;
}

bool testCallbackReentryDuringUndoRedo() {
    std::cout << "TEST: callback re-entry during undo/redo of re-entering command... ";

    CommandHistory history;
    int callbackQueries = 0;

    history.setOnStateChanged([&]() {
        callbackQueries++;
        // Re-enter: query state from within the callback
        [[maybe_unused]] bool cu = history.canUndo();
        [[maybe_unused]] bool cr = history.canRedo();
    });

    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("ReentryTest", &executeCount, &undoCount);
    history.pushAndExecute(cmd);
    assert(callbackQueries == 1);

    // Undo: the cmd->undo() runs outside the lock, then callback fires and queries
    bool undoResult = history.undo();
    assert(undoResult == true);
    assert(callbackQueries == 2);

    // Redo: same pattern
    bool redoResult = history.redo();
    assert(redoResult == true);
    assert(callbackQueries == 3);

    std::cout << "✅ PASS\n";
    return true;
}

bool testMultipleCallbacksReentry() {
    std::cout << "TEST: multiple callbacks all re-entering safely... ";

    CommandHistory history;
    int cb1Count = 0;
    int cb2Count = 0;

    // Register two callbacks, both re-entering CommandHistory
    history.addOnStateChanged([&]() {
        cb1Count++;
        [[maybe_unused]] bool cu = history.canUndo();
        [[maybe_unused]] std::string name = history.getUndoName();
    });
    history.addOnStateChanged([&]() {
        cb2Count++;
        [[maybe_unused]] bool cr = history.canRedo();
        [[maybe_unused]] std::string name = history.getRedoName();
    });

    int executeCount = 0;
    int undoCount = 0;

    auto cmd = std::make_shared<TestCommand>("MultiCb", &executeCount, &undoCount);
    history.pushAndExecute(cmd);

    assert(cb1Count == 1);
    assert(cb2Count == 1);

    history.undo();
    assert(cb1Count == 2);
    assert(cb2Count == 2);

    history.redo();
    assert(cb1Count == 3);
    assert(cb2Count == 3);

    std::cout << "✅ PASS\n";
    return true;
}

int main() {
    std::cout << "=================================\n";
    std::cout << "  CommandHistory Unit Tests\n";
    std::cout << "=================================\n\n";

    int passed = 0;
    int failed = 0;

    struct Test {
        const char* name;
        bool (*func)();
    };

    Test tests[] = {
        {"Push and Execute", testPushAndExecute},
        {"Undo", testUndo},
        {"Redo", testRedo},
        {"Undo/Redo Sequence", testUndoRedoSequence},
        {"Clear Redo on New Command", testClearRedoOnNewCommand},
        {"Empty Undo/Redo", testEmptyUndoRedo},
        {"History Limits", testHistoryLimits},
        {"Clear", testClear},
        {"Callback", testCallback},
        {"Execute Failure", testExecuteFailure},
        {"Undo Failure", testUndoFailure},
        {"Null Command", testNullCommand},
        {"Memory Limit", testMemoryLimit},
        {"Get History Memory Usage", testGetHistoryMemoryUsage},
        {"Memory Limit Unlimited", testMemoryLimitUnlimited},
        {"Callback Re-entry Safety", testCallbackCanQueryHistory},
        {"Callback Re-entry Undo/Redo", testCallbackReentryDuringUndoRedo},
        {"Multiple Callbacks Re-entry", testMultipleCallbacksReentry},
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
