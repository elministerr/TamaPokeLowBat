#!/usr/bin/env python3
"""Install missing cry WAVs over USB, preserving valid recordings already on SD.

  python3 tools/send_cries.py --dir build/pokemon-cries/assets/cries
Requires firmware with CRYINFO and PUT /cries/ support. Does not change pet state.
"""
import argparse
from pathlib import Path
import time
import serial
from prepare_cries import validate_wav
from send_sd import find_port, wait_line


def query(ser, command):
    ser.write((command + "\n").encode())
    lines = []
    end = time.monotonic() + 15
    while time.monotonic() < end:
        line = ser.readline().decode(errors="replace").strip()
        if line == "DONE":
            return "\n".join(lines)
        if line:
            lines.append(line)
    raise RuntimeError(f"No response to {command}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port")
    ap.add_argument("--dir", type=Path, default=Path("tools/sdcard/cries"))
    args = ap.parse_args()
    files = sorted(args.dir.glob("*.wav"))
    if not files:
        ap.error("No WAV files; run tools/prepare_cries.py first")
    uploaded = kept = 0
    with serial.Serial(args.port or find_port(), 115200, timeout=1) as ser:
        for path in files:
            dex = int(path.stem)
            if not 1 <= dex <= 151:
                raise ValueError(f"Invalid species filename: {path}")
            validate_wav(path)
            if "cry=/cries/" in query(ser, f"CRYINFO {dex}"):
                kept += 1
                continue
            ser.write(f"PUT /cries/{dex:03}.wav {path.stat().st_size}\n".encode())
            if not wait_line(ser, "OK", 5):
                raise RuntimeError(f"Device rejected {path.name}")
            with path.open("rb") as source:
                while chunk := source.read(2048):
                    ser.write(chunk)
                    if not wait_line(ser, "#", 5):
                        raise RuntimeError(f"Transfer failed: {path.name}")
            if not wait_line(ser, "DONE", 10) or "cry=/cries/" not in query(ser, f"CRYINFO {dex}"):
                raise RuntimeError(f"Cry validation failed: {path.name}")
            uploaded += 1
            print(f"Installed #{dex:03}", flush=True)
        print(query(ser, "CRYINFO"))
    print(f"Installed {uploaded}; preserved {kept} existing valid cries.")


if __name__ == "__main__":
    main()
