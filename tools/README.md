# tools/

Small, durable one-off utilities to keep the repo healthy.

## brace_check.py

Checks C/C++ brace balance while ignoring braces inside:
- `// ...` line comments
- `/* ... */` block comments
- `"..."` string literals
- `'c'` char literals
- `R"delim( ... )delim"` raw string literals

Usage:

- Balance check:
  - `python tools/brace_check.py Source/Main.cpp`
- Trace which opening brace a `}` closes on a specific line:
  - `python tools/brace_check.py Source/Main.cpp --trace-line 2669`

## Guidelines for future tools

- Prefer single-file scripts with clear CLI flags.
- Avoid interactive input; print deterministic diagnostics.
- Make them PowerShell-friendly (no heredoc assumptions).
