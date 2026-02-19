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

def strip_comments(line, in_block_comment):
    # Remove block comments /* ... */
    # This is a simple state machine approach for single line processing
    # But C++ comments can be tricky.

    clean_line = ""
    i = 0
    n = len(line)

    while i < n:
        if in_block_comment:
            if i + 1 < n and line[i:i+2] == "*/":
                in_block_comment = False
                i += 2
            else:
                i += 1
        else:
            if i + 1 < n and line[i:i+2] == "//":
                break # Rest of line is comment
            elif i + 1 < n and line[i:i+2] == "/*":
                in_block_comment = True
                i += 2
            else:
                clean_line += line[i]
                i += 1

    return clean_line.strip(), in_block_comment

def analyze_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    issues = []
    in_critical_section = False
    brace_count = 0
    in_block_comment = False

    for i, line in enumerate(lines):
        line_num = i + 1

        clean_line, in_block_comment = strip_comments(line, in_block_comment)

        if not clean_line:
            continue

        # Detect function start
        if not in_critical_section:
            for func in CRITICAL_FUNCTIONS:
                # Check for function definition start: "void processBlock (" or "processBlock("
                # We want to avoid matching "processBlock;" (call) or "processBlock = delete" here if possible,
                # but "processBlock(" is a good indicator.
                if re.search(fr"\b{func}\s*\(", clean_line):
                    # Check if it is a declaration (ends with ;)
                    if clean_line.endswith(";") and "{" not in clean_line:
                        continue

                    in_critical_section = True
                    brace_count = 0 # Reset for new function

        if in_critical_section:
            brace_count += clean_line.count('{')
            brace_count -= clean_line.count('}')

            # Scan for keywords
            for pattern, desc in FORBIDDEN_KEYWORDS:
                if re.search(pattern, clean_line):
                    # Special Case: Ignore "= delete"
                    if "= delete" in clean_line:
                        continue

                    issues.append(f"{filepath}:{line_num}: {desc} found in critical section candidate: '{clean_line}'")

            # Check for exit condition
            # If brace_count hits 0 (and we had some braces), we are out.
            # But initial line might not have braces: "void processBlock(...) \n {"
            # So we only exit if brace_count is <= 0 AND we have seen at least one '}' or we are at the end of a one-liner?
            # Or simply: if we are in critical section, and brace_count returns to 0 (or less), we exit.
            # But we must handle the start case where brace_count is 0.
            # Heuristic: if brace_count <= 0 and "}" is in line, we probably exited.
            if brace_count <= 0 and "}" in clean_line:
                in_critical_section = False

    return issues

def main():
    print("Starting Audit...")
    all_issues = []
    for d in SEARCH_DIRS:
        if not os.path.exists(d):
            continue
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
        exit(1) # Fail if issues found
    else:
        print("No obvious real-time safety issues found (heuristic check).")
        if os.path.exists("audit_results.txt"):
            os.remove("audit_results.txt")

if __name__ == "__main__":
    main()
