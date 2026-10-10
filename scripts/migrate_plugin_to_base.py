#!/usr/bin/env python3
"""Migrate one built-in effect onto InternalPluginBase.

Mechanical, and deliberately narrow: it moves the table into a ParamSpec array
and deletes the code the base now owns. It refuses to touch a plugin whose
state blob is not the plain {magic, version, params[]} shape, because those
need migration logic rather than inheritance (AestraEQ carries eight versions).
"""

import re
import sys

PLUGIN = sys.argv[1]
PATH = f"AestraAudio/include/Plugin/{PLUGIN}.h"

src = open(PATH).read()
original = src

# ---------------------------------------------------------------- guard rails
if "struct Blob" not in src:
    sys.exit(f"{PLUGIN}: no Blob found — refusing to guess")

# Every Blob declaration in the file, so the guard below cannot be fooled by
# matching across two of them: both saveState() and loadState() declare one,
# and only the pair has to agree that the layout is the plain one.
# saveState declares `} blob;` and loadState declares `};` — two different
# closing lines, which is why the two are matched separately.
blobs = re.findall(r"struct Blob \{\n((?:.|\n)*?)\n        \}(?: blob;|;)", src)
if not blobs:
    sys.exit(f"{PLUGIN}: no Blob struct found")

plain = {"uint32_t magic = kStateMagic;", "uint32_t version = 1;", "float params[kParamCount] = {};"}
plain_named = {"uint32_t magic;", "uint32_t version;", "float params[kParamCount];"}
for fields in blobs:
    got = {ln.strip() for ln in fields.strip().splitlines() if ln.strip()}
    if got not in (plain, plain_named):
        sys.exit(f"{PLUGIN}: blob is not the plain {{magic, version, params[]}} shape:\n  {sorted(got)}")
if len(blobs) != 2:
    sys.exit(f"{PLUGIN}: expected 2 Blob declarations (save + load), found {len(blobs)}")

# The editor stubs a plugin owns (the base returns false/0 for these).
has_editor = "bool hasEditor() const override { return true; }" in src
size = re.search(r"std::pair<int, int> getEditorSize\(\) const override \{ return \{(\d+), (\d+)\}; \}", src)

def cut(text, start_marker, end_marker, replacement=""):
    """Remove [start_marker, end_marker) and put replacement there."""
    i = text.index(start_marker)
    j = text.index(end_marker, i)
    return text[:i] + replacement + text[j:]

# ------------------------------------------------- 1. header include + base
src = src.replace('#include "Plugin/PluginHost.h"',
                  '#include "Plugin/InternalPluginBase.h"\n#include "Plugin/PluginHost.h"', 1)
src = src.replace(f"class {PLUGIN} : public IPluginInstance {{",
                  f"class {PLUGIN} : public InternalPluginBase {{", 1)

# ------------------------------------------- 2. lift the table out of the class
gp = re.search(
    r"    std::vector<PluginParameter> getParameters\(\) const override \{\n"
    r"        return \{\n(.*?)\n        \};\n    \}\n",
    src, re.S)
if not gp:
    sys.exit(f"{PLUGIN}: could not find the getParameters() table")
rows = gp.group(1)

# Strip the trailing comma from every row but the last, so the array closes
# cleanly. Stripping all of them produces `{...}{...}` — rows run together and
# the compiler reports a stray `{` rather than anything about the commas.
# Array initialiser braces are separators too, not just commas: every row but
# the last needs a trailing comma. Removing them all (or all but the last) makes
# the rows run together and the compiler reports a stray `{`.
lines = [ln.strip() for ln in rows.splitlines() if ln.strip()]
fixed = []
for i, ln in enumerate(lines):
    if not ln.endswith(","):
        ln += ","
    fixed.append(ln)
rows_array = "\n".join("        " + ln for ln in fixed)

contract = f"""    // Declarative parameter table: this is the whole plugin-side surface now.
    // Storage, the non-finite and range guards, getParameters() and the state
    // blob come from InternalPluginBase.
    inline static constexpr ParamSpec kSpecs[] = {{
{rows_array}
    }};

    const ParamSpec* paramSpecs() const override {{ return kSpecs; }}
    uint32_t paramSpecCount() const override {{ return kParamCount; }}
    uint32_t stateMagic() const override {{ return kStateMagic; }}
"""
src = src[:gp.start()] + contract + src[gp.end():]

# ------------------------------- 3. delete what the base now owns, in order
# The cluster ends at getParameterDisplay, which the base supplies a default for
# but these plugins keep (their labels are in musician units). The contract block
# written above sits between getParameters() and getParameterDisplay(), so the
# cut must start after it — otherwise it deletes the ParamSpec table it just
# built and leaves the plugin abstract.
# The cluster runs [getParameterCount .. getParameters()] and then
# getParameterDisplay. Deleting [start of cluster .. contract] would take the
# contract with it, so delete the cluster up to the contract, then delete
# whatever sits between the contract and getParameterDisplay (nothing today,
# but an inherited getParameters() would land there).
i_contract = src.index("    // Declarative parameter table:")
src = cut(src, "    uint32_t getParameterCount() const override", "    // Declarative parameter table:")

src = cut(src, "    std::vector<uint8_t> saveState() const override", "    bool hasEditor() const override")

# ------------------------------------------------- 4. editor stubs worth keeping
# The base supplies the false/0 editor stubs; a plugin that ships an editor
# keeps only hasEditor() and its preferred size. Collapsed into one replace so
# the two lines cannot be emitted twice.
if has_editor:
    keep = "    bool hasEditor() const override { return true; }\n"
    if size:
        keep += f"    std::pair<int, int> getEditorSize() const override {{ return {{{size.group(1)}, {size.group(2)}}}; }}\n"
    # Matched as a whole block, in whatever order the members are declared, and
    # tolerate any spacing — the four stubs are not adjacent in every plugin.
    src, n_keep = re.subn(
        r"[ ]*bool hasEditor\(\) const override \{ return true; \}\n"
        r"(?:[ ]*(?:bool openEditor\(void\*\) override \{ return false; \}|"
        r"void closeEditor\(\) override \{\}|"
        r"bool isEditorOpen\(\) const override \{ return false; \}|"
        r"std::pair<int, int> getEditorSize\(\) const override \{ return \{\d+, \d+\}; \}|"
        r"bool resizeEditor\(int, int\) override \{ return false; \})\n)+",
        keep, src, count=1)
    if n_keep != 1:
        sys.exit(f"{PLUGIN}: could not collapse the editor stubs (matched {n_keep})")

leftovers = [ln.strip() for ln in src.splitlines()
             if re.search(r"\b(openEditor|closeEditor|isEditorOpen|resizeEditor)\b", ln)]
if leftovers:
    sys.exit(f"{PLUGIN}: editor stub members survived:\n  {leftovers}")

# --------------------------------- 5. seeding + the rest of the stub cluster
# The seeding comment differs per plugin, so match the gate rather than the
# prose above it: everything from the `if (!m_paramsInitialized...` line up to
# its closing brace becomes the base's one-shot helper.
# Brace-matched rather than regex-greedy: the loop body is a `for`, and a
# non-greedy `.*?\}\n` stops at the `for`'s closing brace and leaves a stray
# `}` behind.
def replace_gate(text):
    i = text.index("        if (!m_paramsInitialized.exchange(true)) {")
    depth = 0
    j = i
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    # Also drop the comment block immediately above the gate.
    line_start = text.rfind("\n", 0, i) + 1
    k = line_start
    while k > 0:
        prev_end = k - 1
        prev_start = text.rfind("\n", 0, prev_end) + 1
        if text[prev_start:prev_end].strip().startswith("//"):
            k = prev_start
        else:
            break
    return text[:k] + "        seedDefaultsOnce();\n" + text[j + 2:]

src = replace_gate(src)
src = src.replace("    std::atomic<bool> m_paramsInitialized{false};\n", "", 1)
src = src.replace("    std::array<std::atomic<float>, kParamCount> m_params{};\n", "", 1)

# Only the watchdog stubs are unconditional boilerplate. getTailSamples() and
# getInfo() stay: OTT, Filter and LFO report a real non-zero tail, and the base
# returns 0 for it.
src = cut(src, "    WatchdogStats getWatchdogStats() const override { return {}; }\n",
          "    bool isCrashed() const override { return false; }\n")
src = src.replace("    bool isCrashed() const override { return false; }\n", "", 1)

# ------------------------------------------- 6. DSP reads go via the accessors
# `> 0.5f` means "read this as the bypass knob" only when the index really is
# the bypass parameter. A stepped enum (kSyncMode, kType, ...) compares the
# same way but means "is this option selected", so it must keep paramValue().
def _is_bypass(m):
    return m.group(1) == "kBypass"

src = re.sub(r"m_params\[(k\w+)\]\.load\(std::memory_order_relaxed\) > 0\.5f",
             lambda m: "isBypassed()" if _is_bypass(m) else f"paramValue({m.group(1)}) > 0.5f",
             src)
src = re.sub(r"m_params\[(\w+)\]\.load\(std::memory_order_relaxed\)", r"paramValue(\1)", src)

if "m_params[" in src:
    leftover = [ln.strip() for ln in src.splitlines() if "m_params[" in ln]
    sys.exit(f"{PLUGIN}: m_params references survived:\n  {leftover}")

# Every pure virtual of the base must be implemented, or the plugin is abstract
# and make_shared<> fails to compile. Checked here because the failure surfaces
# far away, in whichever TU happens to instantiate the plugin first.
for required in ("paramSpecs() const override", "paramSpecCount() const override",
                 "stateMagic() const override", "getParameterDisplay"):
    if required not in src:
        sys.exit(f"{PLUGIN}: missing '{required}' — the plugin would be abstract")

open(PATH, "w").write(src)
print(f"{PLUGIN}: -{len(original.splitlines())} +{len(src.splitlines())} lines "
      f"({len(original.splitlines()) - len(src.splitlines())} removed)")
