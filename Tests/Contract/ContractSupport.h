// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Shared reporting for the ownership contract tests (label contract:ownership).
//
// These tests encode invariants from the Aestra ownership contract. A test whose
// invariant is violated today is registered with WILL_FAIL and one guards:F#
// label per finding; the pass that fixes the finding removes WILL_FAIL.
//
// WILL_FAIL inverts every non-zero exit, so the exit code must mean exactly one
// thing:
//   * setup/harness failure  -> contractSetup() aborts. An abort is reported as
//     an Exception by ctest and is NOT masked by WILL_FAIL.
//   * invariant violated     -> print "CONTRACT-VIOLATION <F-rows>: ..." and
//     return 1 (masked while the finding is acknowledged as open).
//   * invariant holds        -> print "CONTRACT-HOLDS <F-rows>" and return 0.

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace AestraContract {

inline void contractSetup(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "CONTRACT-SETUP-FAILURE: " << what << std::endl;
        std::abort();
    }
}

class Verdict {
public:
    explicit Verdict(std::string findings) : m_findings(std::move(findings)) {}

    /// Record one invariant check. Observations are printed either way so the
    /// baseline report can quote them.
    void check(bool holds, const std::string& observation) {
        std::cout << (holds ? "  ok:        " : "  violated:  ") << observation << "\n";
        if (!holds) {
            m_violations.push_back(observation);
        }
    }

    int finish() const {
        if (m_violations.empty()) {
            std::cout << "CONTRACT-HOLDS " << m_findings << std::endl;
            return 0;
        }
        for (const auto& v : m_violations) {
            std::cout << "CONTRACT-VIOLATION " << m_findings << ": " << v << "\n";
        }
        std::cout << std::flush;
        return 1;
    }

private:
    std::string m_findings;
    std::vector<std::string> m_violations;
};

/// Select a sub-case by argv[1]; unknown modes are a setup failure.
inline std::string contractMode(int argc, char** argv) {
    contractSetup(argc >= 2, "missing mode argument");
    return argv[1];
}

template <typename T> std::string str(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

} // namespace AestraContract
