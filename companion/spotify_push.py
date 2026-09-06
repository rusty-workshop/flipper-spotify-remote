#!/usr/bin/env python3
"""
Pushes Spotify "now playing" info (title, artist, album art) from this
Linux machine to a Flipper Zero running the Spotify Remote app, over
its USB CLI serial port.

Reads currently-playing metadata from Spotify's MPRIS interface via
`playerctl` (no Spotify API keys / OAuth needed) and downloads the
album art straight from Spotify's CDN URL that MPRIS exposes.

Requires: playerctl (pacman -S playerctl), and the `pyserial` and
`Pillow` Python packages.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

import serial
import serial.tools.list_ports
from PIL import Image

try:
    import requests
except ImportError:
    requests = None

FLIPPER_USB_VID = 0x0483  # STMicroelectronics VCP, used by Flipper Zero
ART_SIZE = 40  # must match SPOTIFY_ART_MAX_DIM in spotify_remote.c
POLL_INTERVAL_S = 2.0


def find_flipper_port() -> str:
    for port in serial.tools.list_ports.comports():
        if port.vid == FLIPPER_USB_VID:
            return port.device
    raise RuntimeError(
        "Couldn't find a Flipper Zero serial port automatically. "
        "Pass --port /dev/ttyACMx explicitly."
    )


def playerctl(*args, player="spotify"):
    try:
        out = subprocess.run(
            ["playerctl", "-p", player, *args],
            capture_output=True,
            text=True,
            timeout=2,
        )
    except FileNotFoundError:
        sys.exit("playerctl not found — install it (e.g. `pacman -S playerctl`).")
    if out.returncode != 0:
        return None
    return out.stdout.strip()


def get_now_playing():
    status = playerctl("status")
    if not status:
        return None

    metadata = playerctl(
        "metadata",
        "--format",
        "{{title}}\t{{artist}}\t{{mpris:artUrl}}",
    )
    if not metadata:
        return None

    parts = metadata.split("\t")
    title = parts[0] if len(parts) > 0 else ""
    artist = parts[1] if len(parts) > 1 else ""
    art_url = parts[2] if len(parts) > 2 else ""

    return {
        "title": title,
        "artist": artist,
        "art_url": art_url,
        "playing": status == "Playing",
    }


def fetch_art_bytes(art_url: str) -> bytes | None:
    if not art_url:
        return None
    try:
        if art_url.startswith("file://"):
            return Path(art_url[len("file://"):]).read_bytes()
        if requests is None:
            print("`requests` not installed — skipping album art", file=sys.stderr)
            return None
        resp = requests.get(art_url, timeout=5)
        resp.raise_for_status()
        return resp.content
    except Exception as exc:  # noqa: BLE001 - best-effort art fetch
        print(f"Failed to fetch album art: {exc}", file=sys.stderr)
        return None


def image_to_xbm_hex(data: bytes) -> str | None:
    try:
        img = Image.open(__import__("io").BytesIO(data)).convert("L")
    except Exception as exc:  # noqa: BLE001
        print(f"Failed to decode album art: {exc}", file=sys.stderr)
        return None

    # Crop to square, then downscale, then dither to 1-bit.
    w, h = img.size
    side = min(w, h)
    img = img.crop(((w - side) // 2, (h - side) // 2, (w + side) // 2, (h + side) // 2))
    img = img.resize((ART_SIZE, ART_SIZE), Image.LANCZOS)
    img = img.convert("1")  # Floyd-Steinberg dithered, 0=black 255=white

    row_bytes = (ART_SIZE + 7) // 8
    buf = bytearray(row_bytes * ART_SIZE)
    pixels = img.load()
    for y in range(ART_SIZE):
        for x in range(ART_SIZE):
            if pixels[x, y] == 0:  # black pixel -> set bit
                buf[y * row_bytes + (x // 8)] |= 1 << (x % 8)

    return buf.hex()


def cli_escape(text: str) -> str:
    return text.replace('"', "'")


class FlipperCli:
    def __init__(self, port: str, baud: int = 115200):
        self.ser = serial.Serial(port, baud, timeout=1)
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def send(self, command: str):
        self.ser.write((command + "\r\n").encode("utf-8"))
        self.ser.flush()
        time.sleep(0.05)
        self.ser.read(self.ser.in_waiting or 1)  # drain echo/response, ignore it

    def close(self):
        self.ser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Flipper CLI serial port (auto-detected if omitted)")
    parser.add_argument(
        "--player", default="spotify", help="MPRIS player name (default: spotify)"
    )
    parser.add_argument(
        "--interval", type=float, default=POLL_INTERVAL_S, help="Poll interval in seconds"
    )
    args = parser.parse_args()

    port = args.port or find_flipper_port()
    print(f"Connecting to Flipper on {port}...")
    flipper = FlipperCli(port)

    last_track_key = None
    last_playing = None

    try:
        while True:
            info = get_now_playing()

            if info is None:
                if last_track_key is not None:
                    flipper.send("spotify_clear")
                    last_track_key = None
                    last_playing = None
                time.sleep(args.interval)
                continue

            track_key = (info["title"], info["artist"])

            if track_key != last_track_key:
                title = cli_escape(info["title"])
                artist = cli_escape(info["artist"])
                state = "playing" if info["playing"] else "paused"
                flipper.send(f'spotify_np "{title}" "{artist}" {state}')
                print(f"Now playing: {info['title']} - {info['artist']} ({state})")

                art_bytes = fetch_art_bytes(info["art_url"])
                if art_bytes:
                    hex_data = image_to_xbm_hex(art_bytes)
                    if hex_data:
                        flipper.send(f"spotify_art {ART_SIZE} {ART_SIZE} {hex_data}")

                last_track_key = track_key
                last_playing = info["playing"]
            elif info["playing"] != last_playing:
                title = cli_escape(info["title"])
                artist = cli_escape(info["artist"])
                state = "playing" if info["playing"] else "paused"
                flipper.send(f'spotify_np "{title}" "{artist}" {state}')
                last_playing = info["playing"]

            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        flipper.close()


if __name__ == "__main__":
    main()
