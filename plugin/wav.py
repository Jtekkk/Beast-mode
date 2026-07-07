#!/usr/bin/env python3
"""Minimal WAV <-> float-text codec — the *host* side of the plugin.

This is deliberately the only non-Beast code in the plugin: it does nothing
but translate between a 16-bit PCM WAV file and the plain-text sample stream
the Beast DSP speaks (sample_rate, channels, then one float sample per line).
All actual audio processing lives in the .bst programs. This mirrors a real
plugin boundary, where the host hands the plugin de-interleaved float buffers
and the plugin never touches the file container.

Usage:
    wav.py to-text   in.wav    >  stream.txt
    wav.py from-text out.wav   <  stream.txt
"""
import sys
import wave


def to_text(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        ch = w.getnchannels()
        width = w.getsampwidth()
        n = w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        sys.exit(f"wav.py: only 16-bit PCM is supported (got {width*8}-bit)")
    import array
    samples = array.array("h")
    samples.frombytes(raw)
    out = sys.stdout
    out.write(f"{sr}\n{ch}\n")
    scale = 1.0 / 32768.0
    write = out.write
    for s in samples:  # already interleaved
        write(f"{s * scale:.9g}\n")


def from_text(path):
    data = sys.stdin.buffer.read().split()
    sr = int(float(data[0]))
    ch = int(float(data[1]))
    import array
    out = array.array("h")
    for tok in data[2:]:
        v = float(tok)
        if v > 1.0:
            v = 1.0
        elif v < -1.0:
            v = -1.0
        out.append(int(round(v * 32767.0)))
    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(out.tobytes())


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "to-text":
        to_text(path)
    elif cmd == "from-text":
        from_text(path)
    else:
        sys.exit(f"wav.py: unknown command '{cmd}'")


if __name__ == "__main__":
    main()
