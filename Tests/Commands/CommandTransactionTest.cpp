// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CommandTransaction Unit Tests
// Tests: CommandTransaction (execute/undo/redo ordering, naming, validity, null handling)

#include "Commands/CommandTransaction.h"
#include "Commands/CommandHistory.h"
#include "Commands/ICommand.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

// =============================================================================
// Helper: TrackingCommand records call order
// =============================================================================

struct TrackingCommand : public ICommand {
    std::vector<std::string>* log;
    std::string tag;

    TrackingCommand(std::vector<std::string>* log, const std::string& tag)
        : log(log), tag(tag) {}

    void execute() override { log->push_back("exec:" + tag); }
    void undo()    override { log->push_back("undo:" + tag); }
    void redo()    override { log->push_back("redo:" + tag); }

    std::string getName() const override { return "Tracking:" + tag; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
};

// A command whose state changes are observable
class CounterCommand : public ICommand {
public:
    CounterCommand(int* counter, int delta)
        : m_counter(counter), m_delta(delta) {}

    void execute() override { *m_counter += m_delta; }
    void undo()    override { *m_counter -= m_delta; }
    void redo()    override { *m_counter += m_delta; }

    std::string getName() const override { return "Counter"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    int* m_counter;
    int m_delta;
};

// =============================================================================
// Tests
// =============================================================================

bool testTransactionDefaultName() {
    std::cout << "TEST: transaction default name... ";

    CommandTransaction txn;
    // Default name is "Transaction" with 0 ops when no commands added
    std::string name = txn.getName();
    assert(!name.empty());

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionCustomName() {
    std::cout << "TEST: transaction custom name... ";

    CommandTransaction txn("Bulk Move");
    std::string name = txn.getName();
    // getName() returns "name (N ops)"
    assert(name.find("Bulk Move") != std::string::npos);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionSetName() {
    std::cout << "TEST: transaction setName... ";

    CommandTransaction txn("Old Name");
    txn.setName("New Name");
    std::string name = txn.getName();
    assert(name.find("New Name") != std::string::npos);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionIsEmptyInitially() {
    std::cout << "TEST: transaction isEmpty initially... ";

    CommandTransaction txn;
    assert(txn.isEmpty() == true);
    assert(txn.size() == 0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionAddCommand() {
    std::cout << "TEST: transaction add command... ";

    CommandTransaction txn;
    int counter = 0;
    txn.add(std::make_shared<CounterCommand>(&counter, 1));

    assert(txn.isEmpty() == false);
    assert(txn.size() == 1);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionAddNullCommandIgnored() {
    std::cout << "TEST: transaction add null command ignored... ";

    CommandTransaction txn;
    txn.add(nullptr); // Should be silently ignored

    assert(txn.isEmpty() == true);
    assert(txn.size() == 0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionIsValidWithCommands() {
    std::cout << "TEST: transaction isValid with commands... ";

    CommandTransaction txn;
    assert(txn.isValid() == false); // Empty = not valid

    int counter = 0;
    txn.add(std::make_shared<CounterCommand>(&counter, 1));
    assert(txn.isValid() == true);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionClear() {
    std::cout << "TEST: transaction clear... ";

    CommandTransaction txn;
    int counter = 0;
    txn.add(std::make_shared<CounterCommand>(&counter, 1));
    txn.add(std::make_shared<CounterCommand>(&counter, 2));
    assert(txn.size() == 2);

    txn.clear();
    assert(txn.isEmpty() == true);
    assert(txn.size() == 0);
    assert(txn.isValid() == false);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionExecuteForwardOrder() {
    std::cout << "TEST: transaction execute forward order... ";

    std::vector<std::string> log;
    CommandTransaction txn("Test");
    txn.add(std::make_shared<TrackingCommand>(&log, "A"));
    txn.add(std::make_shared<TrackingCommand>(&log, "B"));
    txn.add(std::make_shared<TrackingCommand>(&log, "C"));

    txn.execute();

    assert(log.size() == 3);
    assert(log[0] == "exec:A");
    assert(log[1] == "exec:B");
    assert(log[2] == "exec:C");

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionUndoReverseOrder() {
    std::cout << "TEST: transaction undo reverse order... ";

    std::vector<std::string> log;
    CommandTransaction txn("Test");
    txn.add(std::make_shared<TrackingCommand>(&log, "A"));
    txn.add(std::make_shared<TrackingCommand>(&log, "B"));
    txn.add(std::make_shared<TrackingCommand>(&log, "C"));

    txn.execute();
    log.clear();
    txn.undo();

    assert(log.size() == 3);
    assert(log[0] == "undo:C");
    assert(log[1] == "undo:B");
    assert(log[2] == "undo:A");

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionRedoForwardOrder() {
    std::cout << "TEST: transaction redo forward order... ";

    std::vector<std::string> log;
    CommandTransaction txn("Test");
    txn.add(std::make_shared<TrackingCommand>(&log, "A"));
    txn.add(std::make_shared<TrackingCommand>(&log, "B"));
    txn.add(std::make_shared<TrackingCommand>(&log, "C"));

    txn.execute();
    txn.undo();
    log.clear();
    txn.redo();

    assert(log.size() == 3);
    assert(log[0] == "exec:A");
    assert(log[1] == "exec:B");
    assert(log[2] == "exec:C");

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionDoubleExecuteNoOp() {
    std::cout << "TEST: transaction double execute no-op... ";

    int counter = 0;
    CommandTransaction txn("Test");
    txn.add(std::make_shared<CounterCommand>(&counter, 5));

    txn.execute();
    assert(counter == 5);

    // Second execute should be no-op (already executed)
    txn.execute();
    assert(counter == 5);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionUndoBeforeExecuteNoOp() {
    std::cout << "TEST: transaction undo before execute no-op... ";

    int counter = 0;
    CommandTransaction txn("Test");
    txn.add(std::make_shared<CounterCommand>(&counter, 5));

    // Undo before execute - should be no-op
    txn.undo();
    assert(counter == 0);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionAggregatesCounterValues() {
    std::cout << "TEST: transaction aggregates counter values... ";

    int counter = 0;
    CommandTransaction txn("Counter Test");
    txn.add(std::make_shared<CounterCommand>(&counter, 1));
    txn.add(std::make_shared<CounterCommand>(&counter, 2));
    txn.add(std::make_shared<CounterCommand>(&counter, 3));

    txn.execute();
    assert(counter == 6); // 1 + 2 + 3

    txn.undo();
    assert(counter == 0); // All undone

    txn.redo();
    assert(counter == 6); // All redone

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionNameContainsOpCount() {
    std::cout << "TEST: transaction name contains op count... ";

    CommandTransaction txn("Move");
    int counter = 0;
    txn.add(std::make_shared<CounterCommand>(&counter, 1));
    txn.add(std::make_shared<CounterCommand>(&counter, 2));

    std::string name = txn.getName();
    // getName() returns "name (N ops)" per implementation
    assert(name.find("2") != std::string::npos);
    assert(name.find("Move") != std::string::npos);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionIntegrationWithHistory() {
    std::cout << "TEST: transaction integration with CommandHistory... ";

    int counter = 0;
    CommandHistory history;

    // Build a transaction with 3 counter increments
    auto txn = std::make_shared<CommandTransaction>("Bulk Increment");
    txn->add(std::make_shared<CounterCommand>(&counter, 10));
    txn->add(std::make_shared<CounterCommand>(&counter, 20));
    txn->add(std::make_shared<CounterCommand>(&counter, 30));

    history.pushAndExecute(txn);
    assert(counter == 60);
    assert(history.canUndo() == true);
    assert(history.getUndoName().find("Bulk Increment") != std::string::npos);

    // Single undo undoes all 3
    history.undo();
    assert(counter == 0);
    assert(history.canUndo() == false);
    assert(history.canRedo() == true);

    // Single redo redoes all 3
    history.redo();
    assert(counter == 60);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionSingleCommand() {
    std::cout << "TEST: transaction with single command... ";

    int counter = 0;
    CommandTransaction txn("Single");
    txn.add(std::make_shared<CounterCommand>(&counter, 7));

    txn.execute();
    assert(counter == 7);

    txn.undo();
    assert(counter == 0);

    txn.redo();
    assert(counter == 7);

    std::cout << "✅ PASS\n";
    return true;
}

bool testTransactionNullOnlyIsInvalid() {
    std::cout << "TEST: transaction with only null commands is invalid... ";

    CommandTransaction txn("Null Test");
    txn.add(nullptr);
    txn.add(nullptr);

    // Null commands are ignored, so isEmpty is still true
    assert(txn.isEmpty() == true);
    assert(txn.isValid() == false);

    // Execute should not crash even with nothing to run
    txn.execute();

    std::cout << "✅ PASS\n";
    return true;
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    std::cout << "=================================\n";
    std::cout << "  CommandTransaction Unit Tests\n";
    std::cout << "=================================\n\n";

    int passed = 0;
    int failed = 0;

    struct Test {
        const char* name;
        bool (*func)();
    };

    Test tests[] = {
        {"Default Name", testTransactionDefaultName},
        {"Custom Name", testTransactionCustomName},
        {"Set Name", testTransactionSetName},
        {"isEmpty Initially", testTransactionIsEmptyInitially},
        {"Add Command", testTransactionAddCommand},
        {"Add Null Command Ignored", testTransactionAddNullCommandIgnored},
        {"isValid With Commands", testTransactionIsValidWithCommands},
        {"Clear", testTransactionClear},
        {"Execute Forward Order", testTransactionExecuteForwardOrder},
        {"Undo Reverse Order", testTransactionUndoReverseOrder},
        {"Redo Forward Order", testTransactionRedoForwardOrder},
        {"Double Execute No-Op", testTransactionDoubleExecuteNoOp},
        {"Undo Before Execute No-Op", testTransactionUndoBeforeExecuteNoOp},
        {"Aggregates Counter Values", testTransactionAggregatesCounterValues},
        {"Name Contains Op Count", testTransactionNameContainsOpCount},
        {"Integration With History", testTransactionIntegrationWithHistory},
        {"Single Command", testTransactionSingleCommand},
        {"Null-Only Transaction Invalid", testTransactionNullOnlyIsInvalid},
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