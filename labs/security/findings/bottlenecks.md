# Bottlenecks

## Known Performance-Sensitive Paths

These are security guards that run on hot paths and should be monitored for overhead:

1. **WAV parser guards** (`MiniAudioDecoder.cpp`, `MetronomeEngine.cpp`): Input validation on `bitsPerSample`, `dataSize`, `numChannels` runs on every file load. Must be O(1) checks.
2. **JSON depth limit** (`AestraJSON.h`): Recursive depth check adds one integer comparison per nesting level. Negligible on valid files, critical for DoS prevention.
3. **Path traversal guard** (`SamplerPlugin.cpp`): String scanning for `..` and absolute path prefixes on every sample load. Should use `std::string::find` which is efficient.
4. **shellEscape()** (`PlatformUtilsLinux.cpp`): Single-quote wrapping runs on every file dialog invocation. Not a hot path — called once per dialog open.
5. **Plugin cache bounds** (`PluginScanner.cpp`): Count and string length caps on cache load. Not a hot path — cache loaded once at startup. The `file.good()` checks add zero overhead (they check the stream state word, already in cache).
6. **fread return check** (`MetronomeEngine.cpp`): Return value comparison adds one integer comparison. Zero measurable overhead — the comparison is free relative to the I/O cost of `fread` itself.
7. **stoul try/catch** (`ProjectSerializer.cpp`): Exception handling has zero overhead in the common case (no exception thrown). The try/catch block is a compiler annotation, not a runtime check.

## Performance Verdict

All security guards added in S002 have **zero or near-zero performance impact**:
- Bounds checks: O(1) integer comparison
- `file.good()`: Stream state check, already in CPU cache
- try/catch: Zero-cost exception handling (no overhead when no exception)
- fread return value: Single integer comparison, free relative to I/O cost
