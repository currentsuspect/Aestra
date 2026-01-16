
import os
import re

ROOT_DIR = r"C:\Users\Current\Documents\Projects\NOMAD"

def fix_forbidden_strings(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except Exception as e:
        print(f"Skipping {path}: {e}")
        return
    
    original_content = content
    
    # 1. Nomad Studios -> Nomad Studios (Copyright/Ownership)
    content = content.replace("Nomad Studios", "Nomad Studios")
    
    # 2. Aestra Plus -> Aestra Plus (Product name)
    content = content.replace("Aestra Plus", "Aestra Plus")
    
    # 3. Nomad Studios -> Nomad Studios (Catch-all for entity, ONLY if not part of Aestra Plus)
    # But since we replaced Aestra Plus first, "Nomad Studios" remaining is likely the entity.
    # But wait, "Nomad Studios" is forbidden.
    content = content.replace("Nomad Studios", "Nomad Studios")

    if content != original_content:
        print(f"Fixing forbidden strings in: {path}")
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

if __name__ == "__main__":
    for root, dirs, files in os.walk(ROOT_DIR):
        for file in files:
            path = os.path.join(root, file)
            # Process all files, even in External if we messed them up
            fix_forbidden_strings(path)

    print("Forbidden strings fixed.")
