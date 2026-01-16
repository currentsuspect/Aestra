
import os
import re

ROOT_DIR = r"C:\Users\Current\Documents\Projects\Aestra"
DIRS_TO_PROCESS = [
    ".", # Root
    "Source",
    "AestraCore", 
    "AestraPlat", 
    "AestraAudio", 
    "AestraUI",
    "AestraPlugins",
    "Tests"
]

def replace_in_cmake(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    
    original_content = content
    
    # AestraApp.h -> AestraApp.h
    # AestraProfiler.cpp -> AestraProfiler.cpp
    # Regex: Aestra followed by Uppercase letter
    content = re.sub(r'Aestra([A-Z][a-zA-Z0-9_]*)\.(h|cpp|mm|c|rc)', r'Aestra\1.\2', content)

    # Also handle directories in include paths if they match Aestra*
    # e.g. AestraCore/include -> AestraCore/include (already done manually mostly, but good to catch)
    content = re.sub(r'Aestra(Core|Plat|Audio|UI|Plugins)/', r'Aestra\1/', content)
    
    # AestraRumble -> AestraRumble
    content = re.sub(r'AestraRumble', 'AestraRumble', content)

    if content != original_content:
        print(f"Modifying CMakeLists: {path}")
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

if __name__ == "__main__":
    for d in DIRS_TO_PROCESS:
        full_d = os.path.join(ROOT_DIR, d)
        if not os.path.exists(full_d): continue
        
        cmake_path = os.path.join(full_d, "CMakeLists.txt")
        if os.path.exists(cmake_path):
            replace_in_cmake(cmake_path)

    print("CMake filename update complete.")
