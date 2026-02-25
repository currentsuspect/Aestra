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

    for i, line in enumerate(lines):
        line_num = i + 1

        # 1. Strip Comments
        # Basic removal of content after //
        stripped = line.split('//')[0].strip()

        if not stripped:
            continue

        # Ignore Doxygen style comments starting with *
        if stripped.startswith("*"):
            continue

        # 2. Ignore Deleted Functions
        # e.g., "Class(const Class&) = delete;"
        # This contains the keyword "delete" but is safe/compile-time.
        if "= delete" in stripped:
            continue

        # Detect function start
        # Reset state if we find a new critical function definition
        for func in CRITICAL_FUNCTIONS:
            # Look for "void func(" or "int func(" etc.
            # Using a slightly stricter regex to avoid matching function calls inside other functions (imperfect)
            # But checking if we are NOT already in a section or if we are at top level might be better.
            # For now, stick to the simple heuristic but ensure we reset brace count if we think we found a definition.
            if re.search(fr"\b{func}\s*\(", stripped):
                # Only assume it's a definition if it ends with { or implies start
                # This is hard without parsing.
                # Let's assume it IS the start if found.
                if not in_critical_section:
                    in_critical_section = True
                    brace_count = 0

        if in_critical_section:
            # Count braces in the stripped (code) part
            brace_count += stripped.count('{')
            brace_count -= stripped.count('}')

            # Scan for keywords
            for pattern, desc in FORBIDDEN_KEYWORDS:
                if re.search(pattern, stripped):
                    issues.append(f"{filepath}:{line_num}: {desc} found in critical section candidate: '{stripped}'")

            # Check for exit
            if brace_count <= 0 and '}' in stripped:
                in_critical_section = False
                brace_count = 0

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

        # Return error code for CI?
        # Ideally yes, but for now we just report.
    else:
        print("No obvious real-time safety issues found (heuristic check).")

if __name__ == "__main__":
    main()
