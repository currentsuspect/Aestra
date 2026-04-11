# Invariants

1. **No unguarded division on external input**: Any value from a file, env var, or plugin must be validated before use in division, modulo, or array indexing. This includes `bitsPerSample`, `numChannels`, `dataSize`, and any format header field.
2. **No unbounded allocation from external input**: Any size field from a file must be capped at a reasonable maximum before `resize()`, `malloc()`, or `new[]`. This includes WAV `dataSize`, ID3v2 tag size, and JSON array length.
3. **No uncaught `std::stoul`/`std::stoi`/`std::stod` on external input**: All string-to-number conversions from untrusted sources (project files, plugin state, config files) must be wrapped in try/catch with a defined fallback value.
4. **No shell metacaracter injection**: Any value passed to `popen()`, `system()`, or equivalent must be escaped with POSIX single-quote wrapping via `shellEscape()`.
5. **JSON parse depth must be bounded**: Recursive JSON parsing must enforce a hard depth limit (`kMaxJsonDepth`). No unbounded recursion on nested structures.
6. **Path traversal must be rejected**: File paths constructed from project state must reject `..` components and absolute paths outside allowed directories. Sampler loadState, project file references, and plugin paths are all in scope.
7. **No silent crash on malformed input**: Malformed files must produce error returns (false, empty optional, error code), not crashes, NaN, SIGFPE, or undefined behavior.
8. **Every fix must have a proof test**: A security fix is not complete without a test that (a) demonstrated the vuln before the fix, and (b) passes after the fix. No exceptions.
9. **Dual-parser consistency**: If two independent parsers handle the same format (e.g., WAV in MiniAudioDecoder and MetronomeEngine), both must have equivalent input validation guards. A guard in one is not sufficient.
10. **Crash recovery must validate before load**: The crash recovery flow must verify the integrity of the autosave file before loading it. A planted `crash_flag` + crafted `autosave.aes` must not silently load an attacker's project.
