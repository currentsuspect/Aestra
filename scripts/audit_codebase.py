#!/usr/bin/env python3
import os
import re

# Configuration
SEARCH_DIRS = ["AestraAudio/src", "AestraAudio/include"]
CRITICAL_FUNCTIONS = ["processBlock", "process", "processInput"]
FORBIDDEN_KEYWORDS = [
    (r"\bmalloc\b", "Memory allocation (malloc)"),
    (r"(?<!-)\bfree\b", "Memory deallocation (free)"), # Avoid lock-free
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

def strip_comments(line):
    # Remove // comments
    line = re.sub(r'//.*', '', line)
    # Remove /* ... */ comments (simple inline check)
    line = re.sub(r'/\*.*?\*/', '', line)
    return line.strip()

def analyze_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    issues = []
    in_critical_section = False
    brace_count = 0
    seen_brace = False

    for i, line in enumerate(lines):
        line_num = i + 1
        clean_line = strip_comments(line)

        if not clean_line:
            continue

        # Ignore Doxygen continuation lines
        if clean_line.startswith('*') and ('@' in clean_line or 'brief' in clean_line or 'param' in clean_line):
             continue

        # Detect function start
        if not in_critical_section:
            for func in CRITICAL_FUNCTIONS:
                if re.search(fr"\b{func}\s*\(", clean_line):
                    # Check if it ends with semicolon (declaration)
                    if clean_line.endswith(';'):
                        continue

                    in_critical_section = True
                    brace_count = 0
                    seen_brace = False
                    break

        # Process critical section logic (including the starting line)
        if in_critical_section:
            if '{' in clean_line:
                seen_brace = True

            brace_count += clean_line.count('{')
            brace_count -= clean_line.count('}')

            # Scan for keywords
            for pattern, desc in FORBIDDEN_KEYWORDS:
                if re.search(pattern, clean_line):
                    # Double check it's not a false positive like "= delete"
                    if "delete" in pattern and "= delete" in clean_line:
                        continue

                    issues.append(f"{filepath}:{line_num}: {desc} found in critical section candidate: '{clean_line}'")

            # Check exit condition
            if seen_brace and brace_count <= 0:
                in_critical_section = False
            elif not seen_brace and ';' in clean_line and brace_count <= 0:
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
