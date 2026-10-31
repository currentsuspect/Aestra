// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AsyncLoggerMultiProducerTest — #1268: AsyncLogger is the app-wide logger and every thread calls log(), but it queued
// through a single-producer ring. Two threads pushing at once took the same slot: lines were lost, duplicated, or
// written by two threads at once. Producers now take turns; this hammers it from eight threads.

#include "AestraLog.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
// Namespace scope so the producer lambdas can use them without capturing (MSVC requires a capture for locals).
constexpr int kThreads = 8, kPerThread = 100, kRounds = 20; // 800 lines per round: fits the 1024-slot queue

class Capture : public Aestra::ILogger {
public:
    void log(Aestra::LogLevel, const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(message);
    }
    void setLevel(Aestra::LogLevel) override {}
    Aestra::LogLevel getLevel() const override { return Aestra::LogLevel::Debug; }
    std::mutex mutex;
    std::vector<std::string> lines;
};

std::string line(int thread, int index) {
    char buf[96];
    // Long enough that a torn write between two producers shows up as a mismatch.
    std::snprintf(buf, sizeof(buf), "producer-%02d message-%03d xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", thread, index);
    return buf;
}
} // namespace

int main() {
    int bad = 0;
    for (int round = 0; round < kRounds; ++round) {
        auto capture = std::make_shared<Capture>();
        {
            Aestra::AsyncLogger logger(capture);
            std::vector<std::thread> producers;
            for (int t = 0; t < kThreads; ++t)
                producers.emplace_back([&logger, t] {
                    for (int i = 0; i < kPerThread; ++i) logger.log(Aestra::LogLevel::Info, line(t, i));
                });
            for (auto& p : producers) p.join();
        } // the destructor drains what is left
        std::set<std::string> expected, seen;
        for (int t = 0; t < kThreads; ++t)
            for (int i = 0; i < kPerThread; ++i) expected.insert(line(t, i));
        size_t torn = 0, dupes = 0;
        for (const auto& l : capture->lines) {
            if (!expected.count(l)) ++torn;
            else if (!seen.insert(l).second) ++dupes;
        }
        const size_t lost = expected.size() - seen.size();
        if (torn || dupes || lost) {
            ++bad;
            std::printf("round %d: %zu lost, %zu duplicated, %zu torn\n", round, lost, dupes, torn);
        }
    }
    std::cout << (bad == 0 ? "PASS: 20 rounds x 800 lines from 8 threads, every line once and intact\n"
                           : "FAIL: lines lost, duplicated or torn across producers\n");
    return bad == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
