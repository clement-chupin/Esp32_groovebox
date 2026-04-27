#!/usr/bin/env python3
"""
Parse WAVToCode-style .h headers (const int16_t arrays), export to WAV, and play.

Examples:
    python tools/play_h_audio.py --path SOUNDS/SYNTH3.h --symbol SYNTH3 --play
    python tools/play_h_audio.py --path SOUNDS --interactive --play
    python tools/play_h_audio.py --path SOUNDS --list
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import time
import wave
from contextlib import contextmanager
from pathlib import Path
from typing import Dict, List, Optional, Tuple

if sys.platform.startswith("linux"):
    import select
    import termios
    import tty

ARRAY_RE = re.compile(
    r"const\s+int16_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(?:[A-Za-z_][A-Za-z0-9_]*|\d+)?\s*\]\s*(?:PROGMEM\s*)?=\s*\{(.*?)\}\s*;",
    re.DOTALL,
)
INT_RE = re.compile(r"[-+]?\d+")
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//.*?$", re.MULTILINE)


def strip_comments(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return text


def parse_arrays_from_file(file_path: Path) -> Dict[str, List[int]]:
    raw = file_path.read_text(encoding="utf-8", errors="ignore")
    clean = strip_comments(raw)

    arrays: Dict[str, List[int]] = {}
    for match in ARRAY_RE.finditer(clean):
        name = match.group(1)
        body = match.group(2)
        values = [int(v) for v in INT_RE.findall(body)]
        if values:
            arrays[name] = values
    return arrays


def collect_headers(root: Path) -> List[Path]:
    if root.is_file() and root.suffix.lower() == ".h":
        return [root]
    if root.is_dir():
        return sorted([p for p in root.rglob("*.h") if p.is_file()])
    return []


def collect_all_arrays(root: Path) -> List[Tuple[Path, str, List[int]]]:
    found: List[Tuple[Path, str, List[int]]] = []
    for header in collect_headers(root):
        try:
            arrays = parse_arrays_from_file(header)
        except Exception as exc:
            print(f"[WARN] Could not parse {header}: {exc}")
            continue
        for name, data in arrays.items():
            found.append((header, name, data))
    return found


def to_int16(data: List[int], normalize: bool) -> List[int]:
    if not data:
        return data

    if normalize:
        peak = max(abs(v) for v in data) or 1
        scale = 32767.0 / float(peak)
        data = [int(round(v * scale)) for v in data]

    clipped: List[int] = []
    for v in data:
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        clipped.append(v)
    return clipped


def write_wav(path: Path, data: List[int], sample_rate: int) -> None:
    pcm = bytearray()
    for v in data:
        pcm.extend(int(v).to_bytes(2, byteorder="little", signed=True))

    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)


def detect_player() -> str:
    # 1) pure-python backend
    try:
        import simpleaudio as sa  # type: ignore  # noqa: F401

        return "simpleaudio"
    except Exception:
        pass

    if shutil.which("aplay"):
        return "aplay"
    if shutil.which("ffplay"):
        return "ffplay"
    if shutil.which("play"):
        return "play"

    return "none"


def try_play_wav(path: Path) -> bool:
    backend = detect_player()

    if backend == "simpleaudio":
        try:
            import simpleaudio as sa  # type: ignore

            wave_obj = sa.WaveObject.from_wave_file(str(path))
            play_obj = wave_obj.play()
            play_obj.wait_done()
            return True
        except Exception:
            return False

    cmd: Optional[List[str]] = None
    if backend == "aplay":
        cmd = ["aplay", str(path)]
    elif backend == "ffplay":
        cmd = ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", str(path)]
    elif backend == "play":
        cmd = ["play", str(path)]

    if cmd:
        try:
            subprocess.run(cmd, check=True)
            return True
        except Exception:
            return False

    return False


@contextmanager
def cbreak_stdin():
    if not sys.platform.startswith("linux"):
        yield
        return

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        yield
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def read_key_nonblocking() -> Optional[str]:
    if not sys.platform.startswith("linux"):
        return None
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    if not ready:
        return None
    ch = sys.stdin.read(1)
    return ch if ch else None


def _spawn_player(backend: str, wav_path: Path):
    if backend == "simpleaudio":
        import simpleaudio as sa  # type: ignore

        wave_obj = sa.WaveObject.from_wave_file(str(wav_path))
        return wave_obj.play()

    if backend == "aplay":
        return subprocess.Popen(["aplay", str(wav_path)])
    if backend == "ffplay":
        return subprocess.Popen(["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", str(wav_path)])
    if backend == "play":
        return subprocess.Popen(["play", str(wav_path)])

    return None


def _is_done(player) -> bool:
    if player is None:
        return True

    # simpleaudio PlayObject
    if hasattr(player, "is_playing"):
        try:
            return not bool(player.is_playing())
        except Exception:
            return True

    # subprocess Popen
    if hasattr(player, "poll"):
        return player.poll() is not None

    return True


def _stop_player(player) -> None:
    if player is None:
        return

    if hasattr(player, "stop"):
        try:
            player.stop()
        except Exception:
            pass
        return

    if hasattr(player, "terminate"):
        try:
            player.terminate()
        except Exception:
            pass
        try:
            player.wait(timeout=1.0)
        except Exception:
            try:
                player.kill()
            except Exception:
                pass


def play_playlist(items: List[Tuple[Path, str, List[int]]], sample_rate: int, normalize: bool, wav_path: Path) -> int:
    backend = detect_player()
    if backend == "none":
        print("[ERROR] No playback backend found. Install simpleaudio or use aplay/ffplay/play.")
        return 1

    print("Playlist controls: n=next (skip), q=quit")
    print(f"Backend: {backend}")

    with cbreak_stdin():
        for i, (src_file, symbol, data) in enumerate(items, start=1):
            data_i16 = to_int16(data, normalize=normalize)
            write_wav(wav_path, data_i16, sample_rate)
            dur = len(data_i16) / float(sample_rate)
            print(f"\n[{i}/{len(items)}] {symbol}  dur={dur:.3f}s  file={src_file}")

            player = _spawn_player(backend, wav_path)
            if player is None:
                print("[ERROR] Failed to start player")
                return 1

            while True:
                if _is_done(player):
                    break

                key = read_key_nonblocking()
                if key is not None:
                    key = key.lower()
                    if key == "n":
                        _stop_player(player)
                        break
                    if key == "q":
                        _stop_player(player)
                        print("[OK] Playlist stopped by user")
                        return 0

                time.sleep(0.03)

    print("[OK] Playlist complete")
    return 0


def pick_interactive(items: List[Tuple[Path, str, List[int]]], sample_rate: int) -> int:
    print("\nArrays found:")
    for i, (fp, name, data) in enumerate(items):
        dur = len(data) / float(sample_rate)
        print(f"[{i:03d}] {name:<16} samples={len(data):>8}  dur={dur:>7.3f}s  file={fp}")

    while True:
        raw = input("\nChoose index to export/play: ").strip()
        if raw.isdigit():
            idx = int(raw)
            if 0 <= idx < len(items):
                return idx
        print("Invalid index.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract int16 audio arrays from .h files and play/export them.")
    parser.add_argument("--path", required=True, help="Path to a .h file or a folder containing .h files")
    parser.add_argument("--symbol", help="Array symbol name (e.g. SYNTH3, SAMPLE19)")
    parser.add_argument("--rate", type=int, default=16384, help="Sample rate (default: 16384)")
    parser.add_argument("--out", help="Output WAV path (default: ./<SYMBOL>.wav)")
    parser.add_argument("--list", action="store_true", help="Folder mode: play all arrays sequentially (n=skip, q=quit)")
    parser.add_argument("--interactive", action="store_true", help="Interactive chooser")
    parser.add_argument("--play", action="store_true", help="Play WAV after export")
    parser.add_argument("--playlist", action="store_true", help="Play all arrays sequentially (folder mode), with n=skip and q=quit")
    parser.add_argument("--normalize", action="store_true", help="Normalize peak to int16 range")
    args = parser.parse_args()

    root = Path(args.path).expanduser().resolve()
    if not root.exists():
        print(f"[ERROR] Path does not exist: {root}")
        return 1

    items = collect_all_arrays(root)
    if not items:
        print("[ERROR] No const int16_t arrays found.")
        return 1

    if args.playlist:
        if root.is_file():
            print("[ERROR] --playlist expects a folder path")
            return 1
        temp_wav = Path(tempfile.gettempdir()) / "h_audio_playlist_preview.wav"
        return play_playlist(items, sample_rate=args.rate, normalize=args.normalize, wav_path=temp_wav)

    if args.list:
        if root.is_dir():
            temp_wav = Path(tempfile.gettempdir()) / "h_audio_playlist_preview.wav"
            return play_playlist(items, sample_rate=args.rate, normalize=args.normalize, wav_path=temp_wav)

        print("Arrays found:")
        for fp, name, data in items:
            dur = len(data) / float(args.rate)
            print(f"- {name:<16} samples={len(data):>8}  dur={dur:>7.3f}s  file={fp}")
        return 0

    selected: Tuple[Path, str, List[int]] | None = None

    if args.interactive:
        idx = pick_interactive(items, args.rate)
        selected = items[idx]
    elif args.symbol:
        for item in items:
            if item[1] == args.symbol:
                selected = item
                break
        if selected is None:
            print(f"[ERROR] Symbol not found: {args.symbol}")
            return 1
    else:
        if len(items) == 1:
            selected = items[0]
        else:
            print("[ERROR] Multiple arrays found. Use --symbol, --interactive, or --list.")
            return 1

    src_file, symbol, data = selected
    data_i16 = to_int16(data, normalize=args.normalize)

    out_path = Path(args.out).expanduser().resolve() if args.out else Path.cwd() / f"{symbol}.wav"
    write_wav(out_path, data_i16, args.rate)

    dur = len(data_i16) / float(args.rate)
    print(f"[OK] Exported {symbol} from {src_file}")
    print(f"     samples={len(data_i16)}  rate={args.rate} Hz  duration={dur:.3f}s")
    print(f"     wav={out_path}")

    if args.play:
        ok = try_play_wav(out_path)
        if ok:
            print("[OK] Playback finished")
        else:
            print("[WARN] No playback backend found. Install simpleaudio or use aplay/ffplay.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
