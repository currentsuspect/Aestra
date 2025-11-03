// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../include/NomadThreading.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>

using namespace Nomad;

#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "FAILED: " << message << std::endl; \
        return false; \
    }

// =============================================================================
// Lock-Free Ring Buffer Tests
// =============================================================================
bool testLockFreeRingBuffer() {
    std::cout << "Testing LockFreeRingBuffer..." << std::endl;

    LockFreeRingBuffer<int, 8> buffer;

    // Test empty buffer
    TEST_ASSERT(buffer.isEmpty(), "Buffer should be empty initially");
    TEST_ASSERT(!buffer.isFull(), "Buffer should not be full initially");

    // Test push
    TEST_ASSERT(buffer.push(1), "Should push first element");
    TEST_ASSERT(buffer.push(2), "Should push second element");
    TEST_ASSERT(!buffer.isEmpty(), "Buffer should not be empty after push");

    // Test pop
    int value;
    TEST_ASSERT(buffer.pop(value), "Should pop element");
    TEST_ASSERT(value == 1, "Should pop correct value");
    TEST_ASSERT(buffer.pop(value), "Should pop second element");
    TEST_ASSERT(value == 2, "Should pop correct second value");
    TEST_ASSERT(buffer.isEmpty(), "Buffer should be empty after popping all");

    // Test full buffer
    for (int i = 0; i < 7; ++i) {
        TEST_ASSERT(buffer.push(i), "Should push element");
    }
    TEST_ASSERT(buffer.isFull(), "Buffer should be full");
    TEST_ASSERT(!buffer.push(999), "Should not push when full");

    // Test available space
    buffer.pop(value);
    TEST_ASSERT(buffer.available() >= 1, "Should have available space after pop");

    std::cout << "  âœ“ LockFreeRingBuffer tests passed" << std::endl;
    return true;
}

// =============================================================================
// Lock-Free Ring Buffer Thread Safety Test
// =============================================================================
bool testLockFreeRingBufferThreadSafety() {
    std::cout << "Testing LockFreeRingBuffer thread safety..." << std::endl;

    LockFreeRingBuffer<int, 1024> buffer;
    std::atomic<bool> producerDone(false);
    std::atomic<int> itemsProduced(0);
    std::atomic<int> itemsConsumed(0);
    const int totalItems = 10000;

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < totalItems; ++i) {
            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
            itemsProduced.fetch_add(1);
        }
        producerDone = true;
    });

    // Consumer thread
    std::thread consumer([&]() {
        int value;
        while (!producerDone || !buffer.isEmpty()) {
            if (buffer.pop(value)) {
                itemsConsumed++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_ASSERT(itemsProduced == totalItems, "Should produce all items");
    TEST_ASSERT(itemsConsumed == totalItems, "Should consume all items");
    TEST_ASSERT(buffer.isEmpty(), "Buffer should be empty at end");

    std::cout << "  âœ“ LockFreeRingBuffer thread safety tests passed" << std::endl;
    return true;
}

// =============================================================================
// Thread Pool Tests
// =============================================================================
bool testThreadPool() {
    std::cout << "Testing ThreadPool..." << std::endl;

    ThreadPool pool(4);
    TEST_ASSERT(pool.size() == 4, "Thread pool should have 4 threads");

    // Test task execution
    std::atomic<int> counter(0);
    const int numTasks = 100;

    for (int i = 0; i < numTasks; ++i) {
        pool.enqueue([&counter]() {
            counter++;
        });
    }

    // Wait for tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_ASSERT(counter == numTasks, "All tasks should be executed");

    // Test with more complex tasks
    std::atomic<int> sum(0);
    for (int i = 1; i <= 10; ++i) {
        pool.enqueue([&sum, i]() {
            sum += i;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_ASSERT(sum == 55, "Sum should be correct (1+2+...+10 = 55)");

    std::cout << "  âœ“ ThreadPool tests passed" << std::endl;
    return true;
}

// =============================================================================
// Atomic Utilities Tests
// =============================================================================
bool testAtomicUtilities() {
    std::cout << "Testing Atomic Utilities..." << std::endl;

    // Test AtomicFlag
    AtomicFlag flag;
    TEST_ASSERT(!flag.isSet(), "Flag should be clear initially");
    flag.set();
    TEST_ASSERT(flag.isSet(), "Flag should be set");
    flag.clear();
    TEST_ASSERT(!flag.isSet(), "Flag should be clear after clear()");

    // Test testAndSet
    TEST_ASSERT(!flag.testAndSet(), "testAndSet should return false (was clear)");
    TEST_ASSERT(flag.isSet(), "Flag should be set after testAndSet");
    TEST_ASSERT(flag.testAndSet(), "testAndSet should return true (was set)");

    // Test AtomicCounter
    AtomicCounter counter(0);
    TEST_ASSERT(counter.get() == 0, "Counter should be 0 initially");
    TEST_ASSERT(counter.increment() == 1, "Increment should return 1");
    TEST_ASSERT(counter.increment() == 2, "Increment should return 2");
    TEST_ASSERT(counter.get() == 2, "Counter should be 2");
    TEST_ASSERT(counter.decrement() == 1, "Decrement should return 1");
    counter.set(10);
    TEST_ASSERT(counter.get() == 10, "Counter should be 10 after set");

    // Test SpinLock
    SpinLock spinLock;
    int sharedValue = 0;
    
    std::thread t1([&]() {
        for (int i = 0; i < 1000; ++i) {
            spinLock.lock();
            sharedValue++;
            spinLock.unlock();
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 1000; ++i) {
            spinLock.lock();
            sharedValue++;
            spinLock.unlock();
        }
    });

    t1.join();
    t2.join();

    TEST_ASSERT(sharedValue == 2000, "SpinLock should protect shared value");

    std::cout << "  âœ“ Atomic Utilities tests passed" << std::endl;
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  NomadCore Threading Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testLockFreeRingBuffer();
    allPassed &= testLockFreeRingBufferThreadSafety();
    allPassed &= testThreadPool();
    allPassed &= testAtomicUtilities();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  âœ“ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  âœ— SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
