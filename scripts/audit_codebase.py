#!/usr/bin/env python3
import os
import re

# Configuration
SEARCH_DIRS = ["AestraAudio/src", "AestraAudio/include"]
CRITICAL_FUNCTIONS = ["processBlock", "process", "processInput"]
FORBIDDEN_KEYWORDS = [
    (r"\bmalloc\b", "Memory allocation (malloc)"),
    (r"\bfree\b", "Memory deallocation (free)"),
    (r"\bnew\b", "Memory allocation (new)"),
    (r"\bdelete\b", "Memory deallocation (delete)"),
    (r"\bfopen\b", "File I/O (fopen)"),
    (r"\bfread\b", "File I/O (fread)"),
    (r"\bfwrite\b", "File I/O (fwrite)"),
    (r"\bstd::cout\b", "Console Output (std::cout)"),
    (r"\bstd::cerr\b", "Console Output (std::cerr)"),
    (r"\bprintf\b", "Console Output (printf)"),
    (r"\bstd::mutex\b", "Mutex usage (check for locks in RT)"),
    (r"\bstd::lock_guard\b", "Lock guard usage"),
    (r"\bstd::unique_lock\b", "Unique lock usage"),
    (r"\bsleep\b", "Sleep usage"),
    (r"\bstd::this_thread::sleep_for\b", "Thread sleep usage"),
]

def analyze_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    issues = []
    in_critical_section = False
    brace_count = 0

    # Simple state machine to detect if we are inside a critical function
    # This is a heuristic and won't be perfect (e.g. doesn't handle nested classes well without parsing)
    # But for AudioEngine.cpp it should work reasonably well if formatted standardly.

    for i, line in enumerate(lines):
        line_num = i + 1
        stripped = line.strip()

        # Detect function start
        for func in CRITICAL_FUNCTIONS:
            if re.search(fr"\b{func}\s*\(", stripped):
                in_critical_section = True
                # rudimentary brace counting to stay in function
                # Assuming opening brace is on same line or next
                pass

        if in_critical_section:
            brace_count += stripped.count('{')
            brace_count -= stripped.count('}')

            if brace_count <= 0 and '}' in stripped:
                 # Check if we really closed the function?
                 # This is tricky with nested blocks.
                 # Let's just assume if brace_count goes back to 0 (or less if we started at 0) we exited.
                 # But we need to handle the case where the function starts.
                 pass

            # Scan for keywords
            for pattern, desc in FORBIDDEN_KEYWORDS:
                if re.search(pattern, stripped):
                    # Ignore comments (simple check)
                    if stripped.startswith("//") or stripped.startswith("*"):
                        continue
                    if "ALLOW_REALTIME_DELETE" in stripped:
                        continue

                    issues.append(f"{filepath}:{line_num}: {desc} found in critical section candidate: '{stripped}'")

            if brace_count <= 0 and '}' in stripped:
                in_critical_section = False

    return issues

def main():
    print("Starting Audit...")
    all_issues = []
    for d in SEARCH_DIRS:
        for root, _, files in os.walk(d):
            for file in files:
                if file.endswith(".cpp") or file.endswith(".h"):
                    path = os.path.join(root, file)
                    all_issues.extend(analyze_file(path))

    if all_issues:
        print(f"Found {len(all_issues)} potential real-time safety issues:")
        for issue in all_issues:
            print(issue)

        with open("audit_results.txt", "w") as f:
            for issue in all_issues:
                f.write(issue + "\n")
    else:
        print("No obvious real-time safety issues found (heuristic check).")

if __name__ == "__main__":
    main()
