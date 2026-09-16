#!/usr/bin/env python3
"""Prepare original game cries from PokeAPI as mono 16-bit/16 kHz WAV files.

Requires ffmpeg. Audio is downloaded separately from the firmware source.
  python3 tools/prepare_cries.py --out build/pokemon-cries/assets/cries
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from urllib.request import urlopen
import wave


def validate_wav(path):
    with wave.open(str(path), "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getcomptype()) != (1, 2, 16000, "NONE"):
            raise ValueError(f"Unsupported cry format: {path}")
        frames = w.getnframes()
        if not 0 < frames <= 160000 or len(w.readframes(frames)) != frames * 2:
            raise ValueError(f"Empty, truncated or oversized cry: {path}")
        return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("tools/sdcard/cries"))
    parser.add_argument("--revision", default="main", help="PokeAPI/cries revision")
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("ffmpeg is required to convert the source OGG files")
    args.out.mkdir(parents=True, exist_ok=True)
    cache = args.out / ".source"
    cache.mkdir(exist_ok=True)

    def prepare(dex):
        url = f"https://raw.githubusercontent.com/PokeAPI/cries/{args.revision}/cries/pokemon/legacy/{dex}.ogg"
        source = cache / f"{dex}.ogg"
        data = urlopen(url, timeout=30).read()
        source.write_bytes(data)
        dest = args.out / f"{dex:03}.wav"
        subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(source),
                        "-vn", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le",
                        "-map_metadata", "-1", str(dest)], check=True)
        frames = validate_wav(dest)
        return str(dex), {"source": url, "source_sha256": hashlib.sha256(data).hexdigest(),
                          "wav_sha256": hashlib.sha256(dest.read_bytes()).hexdigest(),
                          "samples": frames}

    with ThreadPoolExecutor(max_workers=4) as pool:
        manifest = dict(pool.map(prepare, range(1, 152)))
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared and validated {len(manifest)} cries in {args.out}")


if __name__ == "__main__":
    main()
