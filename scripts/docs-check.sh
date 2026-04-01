#!/bin/bash
# ========================================
# 🧭 Aestra - Documentation Check Script
# ========================================
# Validates Doxygen builds, markdown links, and spelling.
# ----------------------------------------

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}📚 Starting Documentation Check...${NC}"

EXIT_CODE=0

# ----------------------------------------
# 1. Doxygen Check
# ----------------------------------------
if command -v doxygen &> /dev/null; then
    echo -e "\n${YELLOW}Running Doxygen...${NC}"
    # Redirect stdout to null, keep stderr
    if doxygen Doxyfile > /dev/null; then
        echo -e "${GREEN}✓ Doxygen build successful${NC}"

        # Check for warnings in the log file
        if [ -f "doxygen_warnings.log" ] && [ -s "doxygen_warnings.log" ]; then
            WARNING_COUNT=$(wc -l < doxygen_warnings.log)
            echo -e "${RED}✗ Found $WARNING_COUNT Doxygen warnings:${NC}"
            cat doxygen_warnings.log | head -n 10
            if [ "$WARNING_COUNT" -gt 10 ]; then echo "... and more"; fi
            # Fail on warnings? The requirements say "no Doxygen ERROR-level messages" and "WARNINGS minimized".
            # For CI strictness, let's treat warnings as errors if we want "one clean PR".
            # But maybe just warn for now unless strict mode is requested.
            # INSTRUCTION: "Re-run until no Doxygen ERROR-level messages and WARNINGS minimized."
            # "doxygen exit code = 0"
            # So we don't necessarily fail the script on warnings yet, but we report them.
            # However, "SUCCESS CRITERIA" says "doxygen runs with no ERROR (WARNINGS acceptable if documented)".
            # So we pass.
        else
            echo -e "${GREEN}✓ No Doxygen warnings${NC}"
        fi
    else
        echo -e "${RED}✗ Doxygen build failed${NC}"
        EXIT_CODE=1
    fi
else
    echo -e "\n${YELLOW}⚠ Doxygen not installed, skipping build check.${NC}"
fi

# ----------------------------------------
# 2. Markdown Link Check
# ----------------------------------------
echo -e "\n${YELLOW}Checking Markdown Links...${NC}"

CHECKER_CMD=()
if command -v markdown-link-check &> /dev/null; then
    CHECKER_CMD=(markdown-link-check)
elif command -v npx &> /dev/null; then
    CHECKER_CMD=(npx --yes markdown-link-check)
fi

if [ ${#CHECKER_CMD[@]} -gt 0 ]; then
    mapfile -d '' FILES < <(
        find \
            ./docs \
            -type f -name "*.md" \
            -not -path "./docs/api-reference/*" \
            -not -path "./docs/meta/*" \
            -not -path "./docs/TEMPLATE/*" \
            -print0
    )

    if "${CHECKER_CMD[@]}" -q -c .markdown-link-check.json "${FILES[@]}" 2>/dev/null; then
        echo -e "${GREEN}✓ No broken links found${NC}"
    else
        echo -e "${RED}✗ Found broken links!${NC}"
        EXIT_CODE=1
    fi
else
    echo -e "${YELLOW}⚠ markdown-link-check not found, skipping link validation.${NC}"
fi

# ----------------------------------------
# 3. Spelling Check (Optional)
# ----------------------------------------
if command -v codespell &> /dev/null; then
    echo -e "\n${YELLOW}Running Spell Check...${NC}"
    if codespell -S "./node_modules,./.git,./build,./docs/api-reference" -L "uint,nullptr,bool,cant" .; then
        echo -e "${GREEN}✓ Spell check passed${NC}"
    else
        echo -e "${RED}✗ Spell check failed${NC}"
        # Make optional for now
        # EXIT_CODE=1
    fi
fi

echo -e "\n${YELLOW}Done.${NC}"
exit $EXIT_CODE
