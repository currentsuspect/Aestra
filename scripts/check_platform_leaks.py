import argparse
import os
import re
import sys
from pathlib import Path

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
BANNED_INCLUDES = {
    'AestraCore': [
        r'#include\s+<windows\.h>',
        r'#include\s+<alsa/',
        r'#include\s+<jack/',
        r'#include\s+<SDL2/',
        r'#include\s+<X11/',
        r'#include\s+<Cocoa/',
    ],
    'AestraAudioCore': [
        r'#include\s+<windows\.h>',
        r'#include\s+<alsa/',
        r'#include\s+<jack/',
        r'#include\s+<RtAudio\.h>', # RtAudio should only be in AestraAudioLinux/Win/Mac wrapper
    ],
    'AestraPlat': [
        # AestraPlat is interface only, implementations (AestraPlatWin, AestraPlatLinux) can have platform Includes
        # But the common interface headers should not.
        # We'll check the 'include' directory of AestraPlat.
    ]
}

# ---------------------------------------------------------------------------
# Windows platform-leak rules, ported from scripts/check_platform_leaks.ps1
# (which is now a thin wrapper around this file). The PowerShell check scans
# the whole tree for Windows-specific includes/types/macros outside the
# platform implementation directories; this port keeps that rule set here so
# there is exactly one implementation of the check.
# ---------------------------------------------------------------------------

# Repo-relative directories whose contents may use Windows APIs.
# (PowerShell original matched these as path substrings; matching them as
# repo-relative prefixes is the same set for every path in this tree.)
ALLOWED_PATH_PREFIXES = (
    'AestraPlat/src/Win32',
    'AestraAudio/src/Win32',
    'AestraUI/External',  # External libraries (glad, rtaudio) may have Windows code
    'AestraAudio/External',
    # Unconditionally Windows (COM, registry); compiled only by the
    # WIN32-gated AestraAudioWin target (AestraAudio/CMakeLists.txt).
    'AestraAudio/src/Drivers/ASIODriver.cpp',
)

# Forbidden Windows includes (same 11 as the PowerShell check).
FORBIDDEN_WINDOWS_INCLUDES = [
    r'#include\s*[<"]\s*windows\.h\s*[>"]',
    r'#include\s*[<"]\s*winuser\.h\s*[>"]',
    r'#include\s*[<"]\s*dwmapi\.h\s*[>"]',
    r'#include\s*[<"]\s*mmdeviceapi\.h\s*[>"]',
    r'#include\s*[<"]\s*audioclient\.h\s*[>"]',
    r'#include\s*[<"]\s*shellapi\.h\s*[>"]',
    r'#include\s*[<"]\s*shlobj\.h\s*[>"]',
    r'#include\s*[<"]\s*wrl\.h\s*[>"]',
    r'#include\s*[<"]\s*combaseapi\.h\s*[>"]',
    r'#include\s*[<"]\s*objbase\.h\s*[>"]',
    r'#include\s*[<"]\s*ole2\.h\s*[>"]',
]

# Forbidden Windows types in headers outside the Win32 implementation.
FORBIDDEN_WINDOWS_TYPES = [
    'HWND',
    'HINSTANCE',
    'HRESULT',
    'DWORD',
    'HANDLE',
    'LRESULT',
    'WPARAM',
    'LPARAM',
    'GUID',
    'RECT',
    'POINT',
    'MSG',
]

# Forbidden Windows macros in headers outside the Win32 implementation.
FORBIDDEN_WINDOWS_MACROS = [
    'WINAPI',
    'CALLBACK',
    '__stdcall',
    '__declspec',
]

HEADER_EXTS = {'.h', '.hpp'}
SOURCE_EXTS = {'.h', '.hpp', '.cpp', '.c'}

# Windows-only preprocessor guards. A line inside one of these guards is
# compiled only on Windows (or with MSVC, which is Windows-only in practice),
# so it is not a platform leak: conditional compilation is this repo's normal
# idiom for platform separation. AESTRA_COMPILER_MSVC is defined iff
# _MSC_VER (AestraCore/include/AestraConfig.h).
WINDOWS_GUARD_RE = re.compile(r'\b(_WIN32|_WIN64|_MSC_VER|AESTRA_COMPILER_MSVC)\b')
# A preprocessor conditional directive plus its condition text.
PREPROCESSOR_DIRECTIVE_RE = re.compile(r'^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$')
# A '!' applied to the guard test itself (`#if !defined(...)`).
NEGATED_GUARD_RE = re.compile(r'!\s*(defined\b|_WIN32|_WIN64|_MSC_VER|AESTRA_COMPILER_MSVC)')

# Include names are matched case-insensitively (Windows header file names are).
# Types and macros are C identifiers, so they match case-sensitively: the
# PowerShell original's `-match` was case-insensitive only by accident, and
# flagged prose such as "4-point" (POINT) or "typed callback" (CALLBACK).
WINDOWS_INCLUDE_RES = [re.compile(p, re.IGNORECASE) for p in FORBIDDEN_WINDOWS_INCLUDES]
WINDOWS_TYPE_RES = [(t, re.compile(r'\b' + t + r'\b')) for t in FORBIDDEN_WINDOWS_TYPES]
WINDOWS_MACRO_RES = [(m, re.compile(r'\b' + m + r'\b')) for m in FORBIDDEN_WINDOWS_MACROS]


def _guard_tests_windows(condition):
    """True when a preprocessor condition tests a Windows-only macro."""
    return bool(WINDOWS_GUARD_RE.search(condition))


def _guard_is_negated(directive, condition):
    """True when the condition holds on non-Windows (`#ifndef`, `!defined`)."""
    return directive == 'ifndef' or bool(NEGATED_GUARD_RE.search(condition))


def _track_guard_directive(stack, directive, condition):
    """Update the preprocessor-guard stack for one directive line.

    Every `#if*` pushes a frame so `#endif` pairing stays aligned; a line
    counts as guarded only while a Windows guard's Windows branch is open.
    The `#else` of a Windows guard is the POSIX branch, so it is not
    guarded (while the `#else` of `#ifndef _WIN32` is the Windows branch).
    """
    if directive in ('if', 'ifdef', 'ifndef'):
        tests_windows = _guard_tests_windows(condition)
        negated = _guard_is_negated(directive, condition)
        stack.append({
            'sees_windows': tests_windows,
            'negated': negated,
            'in_windows_branch': tests_windows and not negated,
        })
    elif directive == 'elif':
        if stack:
            tests_windows = _guard_tests_windows(condition)
            stack[-1]['sees_windows'] = stack[-1]['sees_windows'] or tests_windows
            stack[-1]['in_windows_branch'] = tests_windows and not _guard_is_negated(directive, condition)
    elif directive == 'else':
        if stack:
            top = stack[-1]
            top['in_windows_branch'] = top['sees_windows'] and top['negated']
    elif directive == 'endif':
        if stack:
            stack.pop()


def _is_guarded(stack):
    """True when any open preprocessor frame is a Windows branch."""
    return any(frame['in_windows_branch'] for frame in stack)


def _strip_trailing_line_comment(line):
    """Remove a trailing `//` comment; prose must not match type rules."""
    idx = line.find('//')
    return line[:idx] if idx != -1 else line


def _update_block_comment_state(line, in_block):
    """Track `/* ... */` blocks across lines.

    Only ever skips more, never less: lines containing `/*` were already
    skipped wholesale by is_skipped_line. This additionally lets the
    type/macro rules skip the prose continuation lines (` * ...`) inside
    the block, e.g. doxygen `@param` prose. Include matching is untouched.
    """
    if in_block:
        # The block ends at the first close; only a reopen after that last
        # close keeps it open.
        return '*/' not in line or '/*' in line.rsplit('*/', 1)[1]
    code = _strip_trailing_line_comment(line)
    return code.count('/*') > code.count('*/')


def is_allowed_path(file_path, project_root):
    """True when the file lives under one of ALLOWED_PATH_PREFIXES."""
    try:
        rel = Path(file_path).resolve().relative_to(Path(project_root).resolve())
    except ValueError:
        return False
    rel_posix = rel.as_posix()
    return any(rel_posix == prefix or rel_posix.startswith(prefix + '/')
               for prefix in ALLOWED_PATH_PREFIXES)


def is_skipped_line(line):
    """Mirror the PowerShell comment skip (leading // or /* anywhere).

    NOLINT / ALLOW_PLATFORM_INCLUDE additionally suppress a line; that is
    this script's pre-existing convention, extended to the ported rules.
    """
    stripped = line.lstrip()
    if stripped.startswith('//'):
        return True
    if '/*' in line:
        return True
    if 'NOLINT' in line or 'ALLOW_PLATFORM_INCLUDE' in line:
        return True
    return False


def scan_file_windows(file_path, project_root):
    """Check one file for the ported Windows-leak rules. Returns messages."""
    leaks = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except (UnicodeDecodeError, OSError):
        return leaks  # Skip binary/unreadable files
    try:
        display = Path(file_path).resolve().relative_to(Path(project_root).resolve()).as_posix()
    except ValueError:
        display = Path(file_path).name
    is_header = Path(file_path).suffix.lower() in HEADER_EXTS
    guard_stack = []
    in_block_comment = False
    for i, line in enumerate(lines, 1):
        if not line.lstrip().startswith('//'):
            directive = PREPROCESSOR_DIRECTIVE_RE.match(line)
            if directive:
                _track_guard_directive(guard_stack, directive.group(1), directive.group(2))
        line_in_block = in_block_comment
        in_block_comment = _update_block_comment_state(line, in_block_comment)
        if is_skipped_line(line):
            continue
        if _is_guarded(guard_stack):
            continue
        for pattern in WINDOWS_INCLUDE_RES:
            if pattern.search(line):
                leaks.append(f"{display}:{i} - Windows include: {line.strip()}")
        if is_header:
            code = line
            if line_in_block:
                if '*/' not in line:
                    continue
                code = line.rsplit('*/', 1)[1]
            # Strip trailing `//` prose before type/macro matching. The
            # NOLINT / ALLOW_PLATFORM_INCLUDE suppression above runs first,
            # so markers living in comments keep working. Includes are
            # matched on the full line: behaviour there is unchanged.
            code = _strip_trailing_line_comment(code)
            for name, rx in WINDOWS_TYPE_RES:
                if rx.search(code):
                    leaks.append(f"{display}:{i} - Windows type: {name}")
            for name, rx in WINDOWS_MACRO_RES:
                if rx.search(code):
                    leaks.append(f"{display}:{i} - Windows macro: {name}")
    return leaks


def check_windows_leaks(project_root):
    """Repo-wide Windows-leak scan (the ported PowerShell rule set)."""
    print("Checking for Windows platform leaks (repo-wide)...")
    root = Path(project_root)
    if not root.exists():
        print(f"Warning: {root} does not exist.")
        return []
    found_leaks = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if Path(name).suffix.lower() not in SOURCE_EXTS:
                continue
            file_path = Path(dirpath) / name
            if is_allowed_path(file_path, root):
                continue
            found_leaks.extend(scan_file_windows(file_path, root))
    return found_leaks

def scan_file(file_path, banned_patterns):
    leaks = []
    guard_stack = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for i, line in enumerate(f, 1):
                if not line.lstrip().startswith('//'):
                    directive = PREPROCESSOR_DIRECTIVE_RE.match(line)
                    if directive:
                        _track_guard_directive(guard_stack, directive.group(1), directive.group(2))
                if _is_guarded(guard_stack):
                    continue
                for pattern in banned_patterns:
                    if re.search(pattern, line):
                        # Exceptions
                        if "NOLINT" in line or "// ALLOW_PLATFORM_INCLUDE" in line:
                            continue
                            
                        leaks.append(f"{file_path.name}:{i} - {line.strip()}")
    except UnicodeDecodeError:
        pass # Skip binary files
    return leaks

def check_module(module_name, relative_path, banned_patterns, recursive=True, project_root=PROJECT_ROOT):
    print(f"Checking {module_name} for platform leaks...")
    base_path = Path(project_root) / relative_path
    
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
    parser = argparse.ArgumentParser(
        description='Detect platform abstraction leaks (cross-platform includes plus '
                    'Windows includes/types/macros outside the Win32 implementation dirs).'
    )
    parser.add_argument('root', nargs='?', default=str(PROJECT_ROOT),
                        help='Repository root (or fixture directory) to scan.')
    parser.add_argument('--fix', action='store_true',
                        help='Accepted for compatibility with check_platform_leaks.ps1 -Fix; '
                             'this script never modifies files, so the flag has no effect.')
    args = parser.parse_args()
    root = Path(args.root)

    total_leaks = 0

    # Check AestraCore
    leaks = check_module('AestraCore', 'AestraCore', BANNED_INCLUDES['AestraCore'],
                         project_root=root)
    if leaks:
        print(f"VIOLATIONS in AestraCore:")
        for leak in leaks:
            print(f"  {leak}")
        total_leaks += len(leaks)

    # Check AestraAudio (Core parts only - headers and generic sources)
    # We might need to be smart about excluding AestraAudio/src/Linux etc if they exist inside
    # based on the folder structure "AestraAudio/src/Linux" they are separate.
    # Assuming generic files are in AestraAudio/src and AestraAudio/include

    # We'll check include first
    leaks = check_module('AestraAudio Headers', 'AestraAudio/include', BANNED_INCLUDES['AestraAudioCore'],
                         project_root=root)
    if leaks:
         print(f"VIOLATIONS in AestraAudio Includes:")
         for leak in leaks:
             print(f"  {leak}")
         total_leaks += len(leaks)

    # Repo-wide Windows-leak scan (ported PowerShell rule set)
    leaks = check_windows_leaks(root)
    if leaks:
        print(f"VIOLATIONS (Windows leaks outside platform implementation dirs):")
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
