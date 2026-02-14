// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace Aestra {

// =============================================================================
// MMCSS (Multimedia Class Scheduler Service) - "Pro Audio" Priority
// =============================================================================
// Dynamically loads Avrt.dll to avoid linker dependencies.
class MMCSS {
public:
    static void setProAudio();
};

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
    alignas(64) T buffer[Size];
    alignas(64) std::atomic<size_t> writeIndex;
    alignas(64) std::atomic<size_t> readIndex;
};

// =============================================================================
// Thread Pool
// =============================================================================
class ThreadPool {
public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

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
    void workerLoop(); // Internal worker loop

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

// =============================================================================
// Synchronization Barrier (Lightweight using atomics)
// =============================================================================
class Barrier {
public:
    explicit Barrier(int count) : counter(count) {}

    // Reset barrier for N expected signals
    void reset(int count) {
        counter.store(count, std::memory_order_release);
    }

    // Signal completion of one task
    void signal() {
        counter.fetch_sub(1, std::memory_order_acq_rel);
    }

    // Wait for all tasks to complete (Spin-wait optimized for low-latency audio)
    void wait() {
        while (counter.load(std::memory_order_acquire) > 0) {
            // In a real-time thread, we prefer spinning over sleeping for short waits.
            // But yielding prevents hogging the core if tasks are long.
            // For audio graph, tasks should be micro-second scale. 
             std::this_thread::yield(); 
        }
    }

private:
    std::atomic<int> counter;
};

// =============================================================================
// Real-Time Thread Pool (Lock-Free Task Dispatch)
// =============================================================================
// Designed for parallel DSP where the RT thread dispatches a batch of tasks
// and waits for them to complete. No mutexes or allocations in dispatch.
class RealTimeThreadPool {
public:
    using TaskFunc = void(*)(void* context, void* taskData);

    RealTimeThreadPool(size_t numThreads);
    ~RealTimeThreadPool();

    // Prepare a batch of tasks. Call from RT thread.
    void dispatch(uint32_t count, void* context, void** taskDataArray, TaskFunc func, Barrier* syncBarrier);

    size_t size() const { return m_workers.size(); }

private:
    void workerLoop(uint32_t threadIdx); // Internal worker loop

    struct Signal {
        std::mutex mtx;
        std::condition_variable cv;
        void notify_all() { cv.notify_all(); }
        template<typename F>
        void wait(F&& cond) {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, std::forward<F>(cond));
        }
    };

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop;
    
    // Shared task state
    TaskFunc m_taskFunc{nullptr};
    void* m_context{nullptr};
    void** m_taskData{nullptr};
    uint32_t m_taskCount{0};
    std::atomic<int> m_taskCounter;
    std::atomic<int> m_activeTasks;
    Barrier* m_syncBarrier{nullptr};

    Signal m_signal;
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

} // namespace Aestra
