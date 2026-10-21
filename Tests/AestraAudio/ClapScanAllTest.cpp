// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-H6: the helper's SCANALL reports every plugin in a CLAP bundle, not only the first. The fixture holds three.
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef HELPER_PATH
#error "HELPER_PATH is set by CMake"
#endif
#ifndef FIXTURE_PATH
#error "FIXTURE_PATH is set by CMake"
#endif

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

std::string hex(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}
std::string unhex(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out += static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
    return out;
}

// One request to a fresh helper; returns its first reply line.
std::string ask(const std::string& request) {
    int in[2], out[2];
    if (pipe(in) != 0 || pipe(out) != 0) return {};
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl(HELPER_PATH, HELPER_PATH, "--stdio", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(in[0]); close(out[1]);
    const std::string line = request + "\nEXIT\n";
    if (write(in[1], line.data(), line.size()) < 0) return {};
    close(in[1]);
    std::string reply;
    char ch;
    while (read(out[0], &ch, 1) == 1 && ch != '\n') reply += ch;
    close(out[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return reply;
}
} // namespace

int main() {
    std::cout << "[H6] SCANALL lists every plugin in the bundle\n";
    const std::string reply = ask("SCANALL clap " + hex(FIXTURE_PATH));
    check(reply.compare(0, 3, "OK ") == 0, "the helper answers OK");

    std::vector<std::vector<std::string>> records;
    std::istringstream all(reply.size() > 3 ? reply.substr(3) : "");
    std::string record;
    while (std::getline(all, record, ';')) {
        std::vector<std::string> fields;
        std::istringstream parts(record);
        std::string part;
        while (std::getline(parts, part, ',')) fields.push_back(unhex(part));
        records.push_back(fields);
    }
    check(records.size() == 3, "all three plugins are reported (not just the first)");
    if (records.size() == 3) {
        check(records[0][0] == "test.aestra.first" && records[1][0] == "test.aestra.second" && records[2][0] == "test.aestra.third",
              "ids arrive in the bundle's order");
        check(records[1][1] == "Second Synth" && records[1][2] == "Aestra Tests" && records[1][3] == "2.0.0", "name, vendor, version arrive");
        check(records[1][4].find("instrument") != std::string::npos, "features arrive, so a synth can be told from an effect");
    }

    std::cout << "[H6] a path that is not a plugin is refused, not guessed\n";
    check(ask("SCANALL clap " + hex("/nonexistent/none.clap")).compare(0, 4, "ERR ") == 0, "missing bundle -> ERR");
    check(ask("SCANALL vst3 " + hex(FIXTURE_PATH)).compare(0, 4, "ERR ") == 0, "other formats -> ERR");

    std::cout << (failures == 0 ? "ALL PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
