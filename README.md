# Spotify Remote (Flipper Zero)

A Spotify companion display + remote for the Flipper Zero. Two independent
links do the work, over two different transports:

- **Control (wireless):** the Flipper pairs as a **BLE HID device** (a
  wireless media-key remote, the same class of thing as a Bluetooth
  headset's playback buttons) and sends standard consumer-control HID
  codes. Spotify — desktop or mobile — already responds to those keys.
- **Now-playing display (USB tether):** Flipper hardware has no Wi-Fi, so
  it can't call the Spotify API to find out what's playing. Instead a
  small companion script on your computer reads Spotify's own MPRIS
  metadata (title, artist, album art) over D-Bus — no Spotify developer
  account or OAuth needed — and pushes it to the Flipper over its USB CLI
  serial port, where the app renders it with a dithered album-art thumbnail.

The display only updates while the companion script is running and the
Flipper is plugged into that computer over USB; the BLE remote controls
work independently and wirelessly at all other times.

Requires [Momentum firmware](https://github.com/Next-Flip/Momentum-Firmware)
(BLE HID profile access). Tested building against Momentum's own source tree.

## Controls

| Button | Action |
|---|---|
| OK | Play / Pause |
| Left | Previous track |
| Right | Next track |
| Up | Volume up |
| Down | Volume down |
| Hold Back | Disconnect and exit |

## Screen

Album art (or a placeholder note glyph, if none has been pushed yet) on
the left, scrolling title/artist on the right, a BLE connection dot in
the corner, and a play/pause + prev/next row below.

## Build

A prebuilt `spotify_remote.fap` is included (built and verified against
Momentum's own source tree). To rebuild from source:

**In-tree (verified working):** clone
[Momentum-Firmware](https://github.com/Next-Flip/Momentum-Firmware), copy
this folder to `applications_user/spotify_remote`, then run
`./fbt fap_spotify_remote`. The built `.fap` lands in
`build/f7-firmware-C/.extapps/spotify_remote.fap`.

**With ufbt:** `pip install ufbt`, then point it at Momentum's SDK index —
check [momentum-fw.dev](https://momentum-fw.dev) or Momentum's Discord for
the current `ufbt update --index-url` value, since that URL isn't
documented in the firmware repo itself and can change. Once configured,
`ufbt build` / `ufbt launch` work as usual.

## Install

Copy `spotify_remote.fap` to `apps/Bluetooth/` on the Flipper's SD card
via qFlipper, then launch it from the Apps menu. (Or `ufbt launch` with
the Flipper connected over USB, once ufbt is configured.)

## Pairing (controls)

Launch the app, then pair "Spotify" as a new Bluetooth device from your
phone or computer's Bluetooth settings (same as pairing headphones).
Once connected, use the D-pad — this works independently of the
now-playing display below.

## Now-playing display (Linux)

Requires `playerctl` and Spotify's official Linux client already running
and playing something (MPRIS only exposes metadata once playback starts).

```bash
sudo pacman -S playerctl   # Arch/Hyprland — use your distro's package manager elsewhere
cd companion
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python3 spotify_push.py
```

Plug the Flipper into this machine over USB (with the Spotify Remote app
running), then run the script — it auto-detects the Flipper's serial
port, polls Spotify's MPRIS metadata every 2 seconds, and pushes track
changes and a dithered 40x40 album-art thumbnail to the app over its
CLI. Pass `--port /dev/ttyACM0` if auto-detection picks the wrong device.

### Wire protocol

The app registers three CLI commands while it's running (visible over
the Flipper's USB CLI regardless of what's on screen):

```
spotify_np "<title>" "<artist>" [playing|paused]
spotify_art <width> <height> <hex-bytes>   # 1bpp XBM, max 40x40, LSB-first per byte
spotify_clear
```

Any serial terminal (or `screen`/`minicom` against the Flipper's CLI
port) can drive these directly if you want to script something other
than Spotify-on-Linux — the app has no idea where the data comes from.

## Verification notes

The firmware side was built and linked successfully against Momentum's
own `fbt` build system (real headers, not guessed APIs). The companion
script's album-art packing was unit-tested against a synthetic image and
produces exactly the byte layout the firmware expects. Neither has been
exercised against a real Flipper or a live Spotify session — I don't have
that hardware here — so treat the first run as the real test.
