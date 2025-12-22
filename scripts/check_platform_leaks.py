import os
import re
import sys
from pathlib import Path

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
BANNED_INCLUDES = {
    'NomadCore': [
        r'#include\s+<windows\.h>',
        r'#include\s+<alsa/',
        r'#include\s+<jack/',
        r'#include\s+<SDL2/',
        r'#include\s+<X11/',
        r'#include\s+<Cocoa/',
    ],
    'NomadAudioCore': [
        r'#include\s+<windows\.h>',
        r'#include\s+<alsa/',
        r'#include\s+<jack/',
        r'#include\s+<RtAudio\.h>', # RtAudio should only be in NomadAudioLinux/Win/Mac wrapper
    ],
    'NomadPlat': [
        # NomadPlat is interface only, implementations (NomadPlatWin, NomadPlatLinux) can have platform Includes
        # But the common interface headers should not.
        # We'll check the 'include' directory of NomadPlat.
    ]
}

def scan_file(file_path, banned_patterns):
    leaks = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for i, line in enumerate(f, 1):
                for pattern in banned_patterns:
                    if re.search(pattern, line):
                        # Exceptions
                        if "NOLINT" in line or "// ALLOW_PLATFORM_INCLUDE" in line:
                            continue
                            
                        leaks.append(f"{file_path.name}:{i} - {line.strip()}")
    except UnicodeDecodeError:
        pass # Skip binary files
    return leaks

def check_module(module_name, relative_path, banned_patterns, recursive=True):
    print(f"Checking {module_name} for platform leaks...")
    base_path = PROJECT_ROOT / relative_path
    
    if not base_path.exists():
        print(f"Warning: {base_path} does not exist.")
        return []

    found_leaks = []
    
    for root, dirs, files in os.walk(base_path):
        for file in files:
            if not file.endswith(('.h', '.cpp', '.hpp', '.c')):
                continue
                
            file_path = Path(root) / file
            
            # Skip build directories or platform-specific implementation folders if necessary
            # For now we assume strict separation
            
            leaks = scan_file(file_path, banned_patterns)
            found_leaks.extend(leaks)
            
        if not recursive:
            break
            
    return found_leaks

def main():
    total_leaks = 0
    
    # Check NomadCore
    leaks = check_module('NomadCore', 'NomadCore', BANNED_INCLUDES['NomadCore'])
    if leaks:
        print(f"VIOLATIONS in NomadCore:")
        for leak in leaks:
            print(f"  {leak}")
        total_leaks += len(leaks)
        
    # Check NomadAudio (Core parts only - headers and generic sources)
    # We might need to be smart about excluding NomadAudio/src/Linux etc if they exist inside
    # based on the folder structure "NomadAudio/src/Linux" they are separate.
    # Assuming generic files are in NomadAudio/src and NomadAudio/include
    
    # We'll check include first
    leaks = check_module('NomadAudio Headers', 'NomadAudio/include', BANNED_INCLUDES['NomadAudioCore'])
    if leaks:
         print(f"VIOLATIONS in NomadAudio Includes:")
         for leak in leaks:
             print(f"  {leak}")
         total_leaks += len(leaks)

    if total_leaks > 0:
        print(f"\nFAILURE: Found {total_leaks} platform abstraction violations.")
        sys.exit(1)
    else:
        print("\nSUCCESS: No platform leaks detected.")
        sys.exit(0)

if __name__ == "__main__":
    main()
