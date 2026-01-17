
import os
import re
import shutil

ROOT_DIR = r"C:\Users\Current\Documents\Projects\Aestra"
DIRS_TO_PROCESS = [
    "Source",
    "AestraCore", 
    "AestraPlat", 
    "AestraAudio", 
    "AestraUI",
    "AestraPlugins",
    "Tests"
]

EXTENSIONS = {".h", ".cpp", ".hpp", ".mm", ".c", ".rc"}

def should_process(path):
    if "External" in path: return False
    _, ext = os.path.splitext(path)
    return ext.lower() in EXTENSIONS

def replace_in_file(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    
    original_content = content
    
    # 1. Namespace replacements
    # namespace Aestra -> namespace Aestra
    content = re.sub(r'namespace\s+Aestra\b', 'namespace Aestra', content)
    # using namespace Aestra -> using namespace Aestra
    content = re.sub(r'using\s+namespace\s+Aestra\b', 'using namespace Aestra', content)
    # Aestra:: -> Aestra::
    content = re.sub(r'\bAestra::', 'Aestra::', content)

    # 2. Include replacements
    # #include "Aestra... -> #include "Aestra...
    # #include <Aestra... -> #include <Aestra...
    def include_replacer(match):
        full_match = match.group(0)
        return full_match.replace("Aestra", "Aestra")
    
    content = re.sub(r'#include\s+["<]Aestra', include_replacer, content)

    # 3. Macro replacements
    # Aestra_API -> AESTRA_API, etc.
    # Exclude Aestra_STUDIOS if it exists? "Aestra Studios" string is safe. 
    # But if there is a define Aestra_STUDIOS, we might want to keep it or rename it?
    # User said: "No 'Aestra Studios'". So if Aestra_STUDIOS is used for company name logic, keep it?
    # Or maybe Aestra_STUDIOS isn't a macro.
    # Let's generally replace Aestra_ with AESTRA_
    content = re.sub(r'\bAestra_', 'AESTRA_', content)

    # 4. Class/Type/Variable replacements
    # AestraApp -> AestraApp
    # AestraAudio -> AestraAudio
    # AestraCore -> AestraCore
    # Aestra[A-Z]... -> Aestra...
    content = re.sub(r'\bAestra([A-Z][a-zA-Z0-9_]*)\b', r'Aestra\1', content)

    # 5. String Literals
    # "Aestra DAW" -> "Aestra DAW"
    content = content.replace('"Aestra DAW"', '"Aestra DAW"')
    content = content.replace('"Aestra"', '"Aestra"')
    
    # "Aestra UI" -> "Aestra UI" (Output messages in CMake)
    content = content.replace('"Aestra UI', '"Aestra UI')
    
    # "Aestra Audio" -> "Aestra Audio"
    content = content.replace('"Aestra Audio', '"Aestra Audio')

    # "Aestra Core" -> "Aestra Core"
    content = content.replace('"Aestra Core', '"Aestra Core')

    # "Aestra Plat" -> "Aestra Plat"
    content = content.replace('"Aestra Plat', '"Aestra Plat')

    # 6. Standalone Aestra (e.g. #ifdef Aestra) -> AESTRA
    # Only if it's a whole word.
    content = re.sub(r'\bAestra\b', 'AESTRA', content)

    if content != original_content:
        print(f"Modifying content: {path}")
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

def rename_files(full_d):
    # Walk bottom-up to rename files without confusing os.walk
    for root, dirs, files in os.walk(full_d, topdown=False):
        if "External" in root: continue
        
        for file in files:
            if file.startswith("Aestra"):
                new_name = file.replace("Aestra", "Aestra", 1) # Only first occurrence to be safe?
                # Actually, AestraAestra.cpp -> AestraAestra.cpp? Unlikely.
                # Just replace "Aestra" at start.
                old_path = os.path.join(root, file)
                new_path = os.path.join(root, new_name)
                print(f"Renaming file: {file} -> {new_name}")
                try:
                    os.rename(old_path, new_path)
                except Exception as e:
                    print(f"Error renaming {file}: {e}")

if __name__ == "__main__":
    # 1. Process Content
    for d in DIRS_TO_PROCESS:
        full_d = os.path.join(ROOT_DIR, d)
        if not os.path.exists(full_d):
            print(f"Skipping {full_d} (not found)")
            continue
            
        for root, dirs, files in os.walk(full_d):
            if "External" in root: continue
            for file in files:
                path = os.path.join(root, file)
                if should_process(path):
                    replace_in_file(path)

    # 2. Rename Files
    for d in DIRS_TO_PROCESS:
        full_d = os.path.join(ROOT_DIR, d)
        if hasattr(os, 'walk'):
            rename_files(full_d)
    
    print("Codebase rename complete.")
