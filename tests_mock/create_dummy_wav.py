import wave
import struct

with wave.open('tests_mock/dummy.wav', 'w') as f:
    f.setnchannels(2)
    f.setsampwidth(4)
    f.setframerate(48000)
    for _ in range(48000):
        f.writeframes(struct.pack('ff', 0.0, 0.0))
