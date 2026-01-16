
import os
import re

ROOT_DIR = r"C:\Users\Current\Documents\Projects\NOMAD"

def replace_resources(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except Exception as e:
        print(f"Skipping {path}: {e}")
        return
    
    original_content = content
    
    # 1. aestra_debug.log -> aestra_debug.log
    content = content.replace("aestra_debug.log", "aestra_debug.log")
    
    # 2. aestra.ico -> aestra.ico
    content = content.replace("aestra.ico", "aestra.ico")
    
    # 3. .aes -> .aes (Project file extension)
    # Be careful not to replace things like "unmd" if that exists (unlikely).
    # Replace ".aes" string literal mostly.
    content = content.replace(".aes", ".aes")
    
    # 4. autosave.aes -> autosave.aes (covered by .aes replace usually, but good to be specific for filenames inside code)
    content = content.replace("autosave.aes", "autosave.aes")

    if content != original_content:
        print(f"Updating resources in: {path}")
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

if __name__ == "__main__":
    for root, dirs, files in os.walk(ROOT_DIR):
        if "External" in root: continue
        if ".git" in root: continue
        
        for file in files:
            path = os.path.join(root, file)
            replace_resources(path)

    print("Resources updated.")
