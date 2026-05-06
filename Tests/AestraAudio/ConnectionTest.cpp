// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Events/Connection.h"

#include <cassert>
#include <iostream>

using namespace Aestra::Events;

void BasicSubscribeEmitTest() {
    Signal<int> signal;
    int callCount = 0;
    int lastValue = 0;

    auto conn = signal.subscribe([&](const int& val) {
        ++callCount;
        lastValue = val;
    });

    signal.emit(42);
    assert(callCount == 1);
    assert(lastValue == 42);
    std::cout << "PASS: BasicSubscribeEmitTest\n";
}

void DisconnectPreventsFutureCallsTest() {
    Signal<int> signal;
    int callCount = 0;

    auto conn = signal.subscribe([&](const int&) { ++callCount; });
    signal.emit(1);
    assert(callCount == 1);

    conn.disconnect();
    signal.emit(2);
    assert(callCount == 1);  // Still 1, callback was disconnected
    std::cout << "PASS: DisconnectPreventsFutureCallsTest\n";
}

void ScopedConnectionRAIItest() {
    Signal<int> signal;
    int callCount = 0;

    {
        ScopedConnection scoped(signal.subscribe([&](const int&) { ++callCount; }));
        signal.emit(1);
        assert(callCount == 1);
    }  // scoped destructs here

    signal.emit(2);
    assert(callCount == 1);  // No more calls after scope exit
    std::cout << "PASS: ScopedConnectionRAIItest\n";
}

void ScopedConnectionsContainerTest() {
    Signal<int> signal;
    int callCount1 = 0;
    int callCount2 = 0;

    ScopedConnections conns;
    conns.add(signal.subscribe([&](const int&) { ++callCount1; }));
    conns.add(signal.subscribe([&](const int&) { ++callCount2; }));

    signal.emit(1);
    assert(callCount1 == 1);
    assert(callCount2 == 1);

    conns.disconnectAll();
    signal.emit(2);
    assert(callCount1 == 1);  // Still 1 after disconnectAll
    assert(callCount2 == 1);
    std::cout << "PASS: ScopedConnectionsContainerTest\n";
}

void VoidSignalTest() {
    Signal<void> signal;
    int callCount = 0;

    auto conn = signal.subscribe([&]() { ++callCount; });
    signal.emit();
    assert(callCount == 1);

    conn.disconnect();
    signal.emit();
    assert(callCount == 1);
    std::cout << "PASS: VoidSignalTest\n";
}

void MoveTransferOwnershipTest() {
    Signal<int> signal;
    int callCount = 0;

    ScopedConnection scoped1(signal.subscribe([&](const int&) { ++callCount; }));
    ScopedConnection scoped2 = std::move(scoped1);
    scoped1.disconnect();  // Should be no-op since ownership moved

    signal.emit(1);
    assert(callCount == 1);  // scoped2 still valid

    scoped2.disconnect();
    signal.emit(2);
    assert(callCount == 1);  // Disconnected
    std::cout << "PASS: MoveTransferOwnershipTest\n";
}

int main() {
    BasicSubscribeEmitTest();
    DisconnectPreventsFutureCallsTest();
    ScopedConnectionRAIItest();
    ScopedConnectionsContainerTest();
    VoidSignalTest();
    MoveTransferOwnershipTest();
    std::cout << "All Connection tests passed\n";
    return 0;
}