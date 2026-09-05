# Spotify Remote (Flipper Zero)

A Bluetooth media remote for the Flipper Zero. It doesn't talk to the
Spotify API — Flipper hardware has no Wi-Fi, so there's no way to hit
Spotify's servers directly. Instead it pairs as a **BLE HID device**
(a wireless media-key remote, the same class of thing as a Bluetooth
headset's playback buttons) and sends standard consumer-control HID
codes. Spotify — desktop or mobile — already responds to those keys,
same as it would from any Bluetooth headset or keyboard.

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

## Pairing

Launch the app, then pair "Spotify" as a new Bluetooth device from your
phone or computer's Bluetooth settings (same as pairing headphones).
Once connected, open Spotify and use the D-pad.
