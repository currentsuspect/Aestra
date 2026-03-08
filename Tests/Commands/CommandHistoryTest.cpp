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

// =============================================================================
// MAIN
// =============================================================================

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
