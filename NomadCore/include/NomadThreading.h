// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace Nomad {

// =============================================================================
// Lock-Free Ring Buffer (Single Producer, Single Consumer)
// =============================================================================
template<typename T, size_t Size>
class LockFreeRingBuffer {
public:
    LockFreeRingBuffer() : writeIndex(0), readIndex(0) {}

    // Push element (returns false if buffer is full)
    bool push(const T& item) {
        size_t currentWrite = writeIndex.load(std::memory_order_relaxed);
        size_t nextWrite = (currentWrite + 1) % Size;
        
        if (nextWrite == readIndex.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        
        buffer[currentWrite] = item;
        writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }

    // Pop element (returns false if buffer is empty)
    bool pop(T& item) {
        size_t currentRead = readIndex.load(std::memory_order_relaxed);
        
        if (currentRead == writeIndex.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }
        
        item = buffer[currentRead];
        readIndex.store((currentRead + 1) % Size, std::memory_order_release);
        return true;
    }

    // Check if buffer is empty
    bool isEmpty() const {
        return readIndex.load(std::memory_order_acquire) == 
               writeIndex.load(std::memory_order_acquire);
    }

    // Check if buffer is full
    bool isFull() const {
        size_t nextWrite = (writeIndex.load(std::memory_order_acquire) + 1) % Size;
        return nextWrite == readIndex.load(std::memory_order_acquire);
    }

    // Get available space
    size_t available() const {
        size_t write = writeIndex.load(std::memory_order_acquire);
        size_t read = readIndex.load(std::memory_order_acquire);
        if (write >= read) {
            return Size - (write - read) - 1;
        }
        return read - write - 1;
    }

    // Current number of elements queued (0..Size-1)
    size_t size() const noexcept {
        size_t write = writeIndex.load(std::memory_order_acquire);
        size_t read = readIndex.load(std::memory_order_acquire);
        if (write >= read) {
            return write - read;
        }
        return Size - (read - write);
    }

    // Maximum usable capacity (one slot is reserved to disambiguate full/empty)
    static constexpr size_t capacity() noexcept {
        return Size - 1;
    }

private:
    T buffer[Size];
    std::atomic<size_t> writeIndex;
    std::atomic<size_t> readIndex;
};

// =============================================================================
// Thread Pool
// =============================================================================
class ThreadPool {
public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency()) 
        : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { 
                            return stop || !tasks.empty(); 
                        });
                        
                        if (stop && tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // Enqueue a task
    template<typename F>
    void enqueue(F&& task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::forward<F>(task));
        }
        condition.notify_one();
    }

    // Get number of worker threads
    size_t size() const {
        return workers.size();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

// =============================================================================
// Atomic Utilities
// =============================================================================

// Atomic flag wrapper for easier usage
class AtomicFlag {
public:
    AtomicFlag() : flag(false) {}
    
    void set() { flag.store(true, std::memory_order_release); }
    void clear() { flag.store(false, std::memory_order_release); }
    bool isSet() const { return flag.load(std::memory_order_acquire); }
    
    // Test and set (returns previous value)
    bool testAndSet() {
        return flag.exchange(true, std::memory_order_acq_rel);
    }

private:
    std::atomic<bool> flag;
};

// Atomic counter
class AtomicCounter {
public:
    AtomicCounter(int initial = 0) : count(initial) {}
    
    int increment() { return count.fetch_add(1, std::memory_order_acq_rel) + 1; }
    int decrement() { return count.fetch_sub(1, std::memory_order_acq_rel) - 1; }
    int get() const { return count.load(std::memory_order_acquire); }
    void set(int value) { count.store(value, std::memory_order_release); }

private:
    std::atomic<int> count;
};

// Spin lock (use sparingly, for very short critical sections)
class SpinLock {
public:
    SpinLock() : flag(false) {}
    
    void lock() {
        while (flag.exchange(true, std::memory_order_acquire)) {
            // Spin
        }
    }
    
    void unlock() {
        flag.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> flag;
};

} // namespace Nomad
