// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// AestraJSON must produce and consume '.' as the decimal separator no matter
// what the process locale is.
//
// This is not hypothetical tidiness. Aestra loads third-party VST3/CLAP
// binaries into its own process, and a plugin calling setlocale(LC_ALL, "")
// is a documented DAW hazard the VST3 SDK itself warns about. Under a
// comma-decimal locale the old code:
//
//   - wrote  1.5  as  "1,5"   -> the .aes file is no longer valid JSON
//   - read   "1.5" as   1     -> silent, unbounded truncation on load
//
// Neither failure reports an error, so nothing but a test catches them.

#include "AestraJSON.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <iostream>
#include <locale>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;
int g_skips = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) {
        std::cout << "  PASS: " << what << "\n";
    } else {
        std::cout << "  FAIL: " << what << "\n";
        ++g_failures;
    }
}

/// A locale whose decimal separator is ',', built from a facet so it works on
/// every platform — no dependency on which locales the machine has installed.
struct CommaNumpunct : std::numpunct<char> {
protected:
    char do_decimal_point() const override { return ','; }
};

bool isCommaDecimalNow() {
    const std::lconv* conv = std::localeconv();
    return conv != nullptr && conv->decimal_point != nullptr &&
           std::strcmp(conv->decimal_point, ",") == 0;
}

const char* tryInstalledCommaLocale() {
    static const char* kCandidates[] = {
        "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8",
        "de-DE",       "fr-FR",      "de_DE",       "fr_FR",
    };
    for (const char* name : kCandidates) {
        if (std::setlocale(LC_ALL, name) != nullptr && isCommaDecimalNow()) {
            return name;
        }
    }
    std::setlocale(LC_ALL, "C");
    return nullptr;
}

/// Build a comma-decimal locale into a temp directory and point LOCPATH at it.
///
/// Without this the end-to-end case silently skips on any runner that ships
/// only C/en_US — which is most Linux CI images. That skip is not harmless:
/// the read-side fix (parsing "1.5" as 1.5 rather than 1) is *only* observable
/// with a real comma C locale, so skipping here would leave that half of the
/// fix unverified while the suite still reported green.
///
/// glibc reads LOCPATH when setlocale() runs, not at process start, so
/// generating and then setting it from inside main() works.
const char* generateCommaCLocale() {
#if defined(__linux__)
    const std::string dir =
        (std::filesystem::temp_directory_path() / "aestra-json-locale-test").string();
    std::error_code ec;
    std::filesystem::create_directories(dir + "/de_DE.UTF-8", ec);
    if (ec) {
        return nullptr;
    }
    // Test-only shell-out: localedef ships with glibc and there is no library
    // API for compiling a locale.
    const std::string cmd =
        "localedef -i de_DE -f UTF-8 \"" + dir + "/de_DE.UTF-8\" >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        return nullptr;
    }
    ::setenv("LOCPATH", dir.c_str(), 1);
    if (std::setlocale(LC_ALL, "de_DE.UTF-8") != nullptr && isCommaDecimalNow()) {
        return "de_DE.UTF-8 (generated)";
    }
#endif
    std::setlocale(LC_ALL, "C");
    return nullptr;
}

/// Put the *C* locale into comma-decimal mode by any means available.
const char* activateCommaCLocale() {
    if (const char* installed = tryInstalledCommaLocale()) {
        return installed;
    }
    return generateCommaCLocale();
}

// ---------------------------------------------------------------------------

/// The separator rewrite, driven directly. This is the part that has to work
/// on every platform, so it is tested without depending on installed locales.
void testSeparatorRewrite() {
    std::cout << "\n[separator rewrite — no OS locale required]\n";

    char buf[64];

    // Bounded copy rather than strcpy: every literal here is far under 64
    // bytes, but SAST flags the unbounded form (CWE-120) and there is no
    // reason to re-justify that on every review pass.
    const auto rewrite = [&buf](const char* input, const char* separator) {
        std::snprintf(buf, sizeof(buf), "%s", input);
        Aestra::json_detail::normalizeDecimalPointToDot(buf, sizeof(buf), separator);
    };

    rewrite("1,5", ",");
    check(std::strcmp(buf, "1.5") == 0, "comma separator is rewritten to '.'");

    rewrite("-1024,125", ",");
    check(std::strcmp(buf, "-1024.125") == 0, "negative value keeps its sign through the rewrite");

    rewrite("42", ",");
    check(std::strcmp(buf, "42") == 0, "integral value with no separator is untouched");

    rewrite("1.5", ".");
    check(std::strcmp(buf, "1.5") == 0, "'.' locale is a no-op");

    // POSIX permits a multi-byte separator; the tail must close up behind it.
    rewrite("3<SEP>25", "<SEP>");
    check(std::strcmp(buf, "3.25") == 0, "multi-byte separator collapses to a single '.'");
}

/// Guards the parse path against a std::locale::global installed by a host or
/// a plugin. Portable: uses a facet, not an installed locale.
void testGlobalCppLocaleDoesNotLeak() {
    std::cout << "\n[global C++ locale set to comma]\n";

    const std::locale previous = std::locale();
    std::locale::global(std::locale(std::locale::classic(), new CommaNumpunct));

    // Prove the mutation actually took effect — otherwise the assertions below
    // would pass simply because nothing changed.
    std::ostringstream probe;
    probe << 1.5;
    check(probe.str() == "1,5", "precondition: global C++ locale really is comma-decimal");

    bool consumedAll = false;
    const Aestra::JSON parsed = Aestra::JSON::parseStrict("{\"v\":1.5}", consumedAll);
    check(consumedAll, "parse succeeds under a comma global locale");
    check(parsed["v"].asNumber() == 1.5, "\"1.5\" parses to 1.5, not 1");

    Aestra::JSON root = Aestra::JSON::object();
    root.set("v", Aestra::JSON(1.5));
    check(root.toString(0).find("1.5") != std::string::npos,
          "serialised output still uses '.'");

    std::locale::global(previous);
}

/// The end-to-end reproduction of the original bug. Only runs where the
/// machine actually has a comma-decimal locale installed.
void testCLocaleRoundTrip() {
    std::cout << "\n[C locale set to comma — end-to-end]\n";

    const char* name = activateCommaCLocale();
    if (name == nullptr) {
        std::cout << "  SKIPPED: no comma-decimal locale installed, and generating one\n"
                  << "           failed. The read-side fix is UNVERIFIED in this run.\n";
        ++g_skips;
        return;
    }

    std::cout << "  (using locale: " << name << ")\n";

    // Precondition: prove the locale is genuinely comma-decimal, so a silently
    // failed setlocale cannot make the rest of this function pass vacuously.
    char probe[32];
    std::snprintf(probe, sizeof(probe), "%.1f", 1.5);
    check(std::strcmp(probe, "1,5") == 0, "precondition: printf really emits a comma here");

    Aestra::JSON root = Aestra::JSON::object();
    root.set("startBeat", Aestra::JSON(1024.125));
    root.set("gain", Aestra::JSON(-6.5));
    const std::string text = root.toString(0);

    check(text.find("1024.125") != std::string::npos, "writes 1024.125 with a '.' separator");
    check(text.find(',') == std::string::npos || text.find("1024,125") == std::string::npos,
          "no comma appears inside a number");

    bool consumedAll = false;
    const Aestra::JSON reloaded = Aestra::JSON::parseStrict(text, consumedAll);
    check(consumedAll, "its own output reparses as valid JSON");
    check(reloaded["startBeat"].asNumber() == 1024.125, "1024.125 survives the round trip exactly");
    check(reloaded["gain"].asNumber() == -6.5, "-6.5 survives the round trip exactly");

    std::setlocale(LC_ALL, "C");
}

/// The change must not cost the precision the formatter was written for.
void testPrecisionUnchanged() {
    std::cout << "\n[precision]\n";

    const double values[] = {1024.125, -6.5, 0.1, 1e9, -0.0009765625, 3.141592653589793};
    for (double v : values) {
        Aestra::JSON root = Aestra::JSON::object();
        root.set("v", Aestra::JSON(v));
        bool consumedAll = false;
        const Aestra::JSON back = Aestra::JSON::parseStrict(root.toString(0), consumedAll);
        check(consumedAll && back["v"].asNumber() == v,
              "exact round trip for " + std::to_string(v));
    }
}

} // namespace

int main() {
    std::cout << "============================================\n"
              << "  AestraJSON Locale Independence Tests\n"
              << "============================================\n";

    testSeparatorRewrite();
    testGlobalCppLocaleDoesNotLeak();
    testCLocaleRoundTrip();
    testPrecisionUnchanged();

    std::cout << "\n============================================\n";
    if (g_failures == 0) {
        std::cout << "  All " << g_checks << " checks passed.\n";
        if (g_skips > 0) {
            std::cout << "  WARNING: " << g_skips
                      << " case(s) skipped — coverage is incomplete in this run.\n";
        }
    } else {
        std::cout << "  " << g_failures << " of " << g_checks << " checks FAILED.\n";
    }
    std::cout << "============================================\n";
    return g_failures == 0 ? 0 : 1;
}
