// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MacroCommand Unit Tests
// Tests: multi-command grouping, reverse-order undo, naming

#include "Commands/MacroCommand.h"

#include "Commands/CommandHistory.h"
#include "Commands/ICommand.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

// Test command that records execution order
class TrackingCommand : public ICommand {
public:
    TrackingCommand(const std::string& name, std::vector<std::string>* log)
        : m_name(name), m_log(log), m_executed(false) {}

    void execute() override {
        m_log->push_back("EXEC:" + m_name);
        m_executed = true;
    }

    void undo() override {
        m_log->push_back("UNDO:" + m_name);
        m_executed = false;
    }

    void redo() override {
        m_log->push_back("REDO:" + m_name);
        m_executed = true;
    }

    std::string getName() const override { return m_name; }
    bool isExecuted() const { return m_executed; }

private:
    std::string m_name;
    std::vector<std::string>* m_log;
    bool m_executed;
};

// State-changing command for integration tests
class CounterCommand : public ICommand {
public:
    CounterCommand(int* counter, int delta) : m_counter(counter), m_delta(delta) {}

    void execute() override { *m_counter += m_delta; }

    void undo() override { *m_counter -= m_delta; }

    void redo() override { *m_counter += m_delta; }

    std::string getName() const override { return "Counter" + std::to_string(m_delta); }

private:
    int* m_counter;
    int m_delta;
};

// =============================================================================
// TEST FUNCTIONS
// =============================================================================

bool testEmptyMacro() {
    std::cout << "TEST: empty macro... ";

    MacroCommand macro;

    // Should not crash
    macro.execute();
    macro.undo();
    macro.redo();

    assert(macro.getName() == "Macro");
    assert(macro.empty() == true);
    assert(macro.getCommandCount() == 0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testSingleCommand() {
    std::cout << "TEST: single command in macro... ";

    std::vector<std::string> log;
    MacroCommand macro;

    auto cmd = std::make_shared<TrackingCommand>("A", &log);
    macro.addCommand(cmd);

    assert(macro.empty() == false);
    assert(macro.getCommandCount() == 1);

    macro.execute();
    assert(log.size() == 1);
    assert(log[0] == "EXEC:A");

    macro.undo();
    assert(log.size() == 2);
    assert(log[1] == "UNDO:A");

    macro.redo();
    assert(log.size() == 3);
    assert(log[2] == "REDO:A");

    std::cout << "✅ PASS\n";
    return true;
}

bool testMultipleCommandsExecutionOrder() {
    std::cout << "TEST: multiple commands execution order... ";

    std::vector<std::string> log;
    MacroCommand macro;

    macro.addCommand(std::make_shared<TrackingCommand>("A", &log));
    macro.addCommand(std::make_shared<TrackingCommand>("B", &log));
    macro.addCommand(std::make_shared<TrackingCommand>("C", &log));

    macro.execute();

    // Should execute in order: A, B, C
    assert(log.size() == 3);
    assert(log[0] == "EXEC:A");
    assert(log[1] == "EXEC:B");
    assert(log[2] == "EXEC:C");

    std::cout << "✅ PASS\n";
    return true;
}

bool testUndoReverseOrder() {
    std::cout << "TEST: undo in reverse order... ";

    std::vector<std::string> log;
    MacroCommand macro;

    macro.addCommand(std::make_shared<TrackingCommand>("A", &log));
    macro.addCommand(std::make_shared<TrackingCommand>("B", &log));
    macro.addCommand(std::make_shared<TrackingCommand>("C", &log));

    macro.execute();
    log.clear();

    macro.undo();

    // Should undo in reverse: C, B, A
    assert(log.size() == 3);
    assert(log[0] == "UNDO:C");
    assert(log[1] == "UNDO:B");
    assert(log[2] == "UNDO:A");

    std::cout << "✅ PASS\n";
    return true;
}

bool testRedoForwardOrder() {
    std::cout << "TEST: redo in forward order... ";

    std::vector<std::string> log;
    MacroCommand macro;

    macro.addCommand(std::make_shared<TrackingCommand>("A", &log));
    macro.addCommand(std::make_shared<TrackingCommand>("B", &log));

    macro.execute();
    macro.undo();
    log.clear();

    macro.redo();

    // Should redo in order: A, B
    assert(log.size() == 2);
    assert(log[0] == "REDO:A");
    assert(log[1] == "REDO:B");

    std::cout << "✅ PASS\n";
    return true;
}

bool testSetName() {
    std::cout << "TEST: set name... ";

    MacroCommand macro;
    assert(macro.getName() == "Macro");

    macro.setName("Move 5 Clips");
    assert(macro.getName() == "Move 5 Clips");

    std::cout << "✅ PASS\n";
    return true;
}

bool testConstructorWithName() {
    std::cout << "TEST: constructor with name... ";

    MacroCommand macro("Batch Edit");
    assert(macro.getName() == "Batch Edit");

    std::cout << "✅ PASS\n";
    return true;
}

bool testNullCommandIgnored() {
    std::cout << "TEST: null command ignored... ";

    MacroCommand macro;

    macro.addCommand(nullptr);
    macro.addCommand(std::make_shared<TrackingCommand>("A", nullptr));
    macro.addCommand(nullptr);

    assert(macro.getCommandCount() == 1);

    std::cout << "✅ PASS\n";
    return true;
}

bool testStateAccumulation() {
    std::cout << "TEST: state accumulation with counters... ";

    int counter = 0;
    MacroCommand macro;

    macro.addCommand(std::make_shared<CounterCommand>(&counter, 10));
    macro.addCommand(std::make_shared<CounterCommand>(&counter, 5));
    macro.addCommand(std::make_shared<CounterCommand>(&counter, 3));

    // Execute: 0 + 10 + 5 + 3 = 18
    macro.execute();
    assert(counter == 18);

    // Undo: 18 - 3 - 5 - 10 = 0
    macro.undo();
    assert(counter == 0);

    // Redo: 0 + 10 + 5 + 3 = 18
    macro.redo();
    assert(counter == 18);

    std::cout << "✅ PASS\n";
    return true;
}

bool testChangesProjectStateAnyTrue() {
    std::cout << "TEST: changesProjectState if any command changes state... ";

    // Command that doesn't change state
    class NoChangeCommand : public ICommand {
    public:
        void execute() override {}
        void undo() override {}
        void redo() override { execute(); }
        std::string getName() const override { return "NoChange"; }
        bool changesProjectState() const override { return false; }
    };

    MacroCommand macro;

    // All no-change commands
    macro.addCommand(std::make_shared<NoChangeCommand>());
    macro.addCommand(std::make_shared<NoChangeCommand>());

    assert(macro.changesProjectState() == false);

    // Add one that does change
    int counter = 0;
    macro.addCommand(std::make_shared<CounterCommand>(&counter, 1));

    assert(macro.changesProjectState() == true);

    std::cout << "✅ PASS\n";
    return true;
}

bool testGetSizeInBytes() {
    std::cout << "TEST: getSizeInBytes sums children... ";

    int counter = 0;
    MacroCommand macro;

    auto cmd1 = std::make_shared<CounterCommand>(&counter, 1);
    auto cmd2 = std::make_shared<CounterCommand>(&counter, 2);

    macro.addCommand(cmd1);
    macro.addCommand(cmd2);

    size_t macroSize = macro.getSizeInBytes();
    size_t expectedSize = sizeof(MacroCommand) + cmd1->getSizeInBytes() + cmd2->getSizeInBytes();

    assert(macroSize == expectedSize);

    std::cout << "✅ PASS\n";
    return true;
}

// =============================================================================
// INTEGRATION: MacroCommand + CommandHistory
// =============================================================================

bool testHistoryIntegration() {
    std::cout << "TEST: MacroCommand + CommandHistory integration... ";

    CommandHistory history;
    int counter = 0;

    auto macro = std::make_shared<MacroCommand>();
    macro->setName("Batch Update");

    macro->addCommand(std::make_shared<CounterCommand>(&counter, 10));
    macro->addCommand(std::make_shared<CounterCommand>(&counter, 20));
    macro->addCommand(std::make_shared<CounterCommand>(&counter, 30));

    history.pushAndExecute(macro);

    assert(counter == 60);
    assert(history.canUndo() == true);
    assert(history.getUndoName() == "Batch Update");

    history.undo();
    assert(counter == 0);

    history.redo();
    assert(counter == 60);

    std::cout << "✅ PASS\n";
    return true;
}

bool testNestedMacros() {
    std::cout << "TEST: nested macros... ";

    int counter = 0;

    auto inner = std::make_shared<MacroCommand>("Inner");
    inner->addCommand(std::make_shared<CounterCommand>(&counter, 5));
    inner->addCommand(std::make_shared<CounterCommand>(&counter, 5));

    auto outer = std::make_shared<MacroCommand>("Outer");
    outer->addCommand(std::make_shared<CounterCommand>(&counter, 10));
    outer->addCommand(inner);
    outer->addCommand(std::make_shared<CounterCommand>(&counter, 10));

    // Execute: 10 + (5 + 5) + 10 = 30
    outer->execute();
    assert(counter == 30);

    // Undo: -10 -5 -5 -10 = -30
    outer->undo();
    assert(counter == 0);

    std::cout << "✅ PASS\n";
    return true;
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    std::cout << "=================================\n";
    std::cout << "  MacroCommand Unit Tests\n";
    std::cout << "=================================\n\n";

    int passed = 0;
    int failed = 0;

    struct Test {
        const char* name;
        bool (*func)();
    };

    Test tests[] = {
        {"Empty macro", testEmptyMacro},
        {"Single command", testSingleCommand},
        {"Multiple commands execution order", testMultipleCommandsExecutionOrder},
        {"Undo reverse order", testUndoReverseOrder},
        {"Redo forward order", testRedoForwardOrder},
        {"Set name", testSetName},
        {"Constructor with name", testConstructorWithName},
        {"Null command ignored", testNullCommandIgnored},
        {"State accumulation", testStateAccumulation},
        {"Changes project state", testChangesProjectStateAnyTrue},
        {"Get size in bytes", testGetSizeInBytes},
        {"History integration", testHistoryIntegration},
        {"Nested macros", testNestedMacros},
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
