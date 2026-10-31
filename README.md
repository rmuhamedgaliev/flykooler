# Flydigi Kooler

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Fan, lighting and gear control for **Flydigi BS-series** laptop coolers on
Linux — a native KDE Plasma / Qt 6 application with a client-server design.

## Supported hardware

| Cooler | PID | Status |
| --- | --- | --- |
| BS1 | `37d7:1001` | Not supported: it speaks BLE/GATT, not HID |
| BS2 Pro | `37d7:1002` | Tested over USB and Bluetooth |
| BS3 | `37d7:1003` | Same protocol, untested on hardware |
| BS3 Pro | `37d7:1004` | Same protocol, untested on hardware |

Devices with any other PID are ignored instead of being sent frames their
firmware does not expect.

The protocol was captured from a live BS2 Pro (`5A A5` frames, 0xEF status
frames with the 9-byte BS2 Pro payload) and cross-checked against the BS3 Pro
firmware disassembly from the
[flydigictl](https://github.com/ElXreno/flydigictl) project.

## Features

- **Three fan modes**: a smart temperature curve (any hwmon sensor), a fixed
  speed (500–4000 rpm), or the four gears of the device button (with a gear
  speed editor stored in the cooler)
- **Lighting**: steady colour or 5 built-in effects, brightness, gear
  indicator LEDs — uploaded to the cooler and surviving power cycles
- Live status: rpm, target, gear, power supply level with the rpm ceiling
  (2700 / 3300 / 4000)
- Plasma tray: mode switching, fan stop, connect notifications
- Auto-connection over cable or Bluetooth with hot reconnect
- Client-server design: the backend owns the device, GUI and CLI are clients
  on a Unix socket, the fan curve keeps running with the window closed
- CLI for scripting: `flykooler cli status|set|gear|light|…`
- UI translated (Russian included; picked by the system locale)

## Architecture

```
┌─────────────────┐   Unix socket    ┌──────────────────┐
│  flykoolerd     │◄────────────────►│  flykooler (GUI) │
│  backend        │  JSON, lines     │  thin client     │
│  owns the device│                  └──────────────────┘
│  systemd unit   │◄────────────────►┌──────────────────┐
└─────────────────┘                  │ flykooler cli    │
                                     └──────────────────┘
```

The backend runs as a systemd **user** service (the fan curve works even
with the window closed). The GUI starts the backend itself if it is not
running. Dangerous firmware commands (`0xDF`, `0x06`) are blocked in code.

## Build and install

```sh
make                        # build (Release)
sudo make install           # into /usr/local
# or without root:
make install PREFIX=$HOME/.local
```

Targets: `make run` (build + run the GUI), `make clean`, `make distclean`,
`make uninstall`. Under the hood it is CMake; the direct route works too:
`cmake -B build -G Ninja && cmake --build build && cmake --install build`.

### Dependencies

| What | Why | Arch / CachyOS | Debian / Ubuntu | Fedora |
| --- | --- | --- | --- | --- |
| Qt 6.4+ (Core, Network) | backend, CLI | `qt6-base` | `qt6-base-dev` | `qt6-qtbase-devel` |
| Qt 6 Widgets | GUI (optional) | `qt6-base` | `qt6-base-dev` | `qt6-qtbase-devel` |
| cmake ≥ 3.24, a C++20 compiler | build | `base-devel cmake` | `build-essential cmake` | `gcc-c++ cmake` |

Without Qt6 Widgets the project builds backend-only (`flykoolerd` + CLI) —
handy on headless machines: `cmake -B build -DFLYKOOLER_GUI=OFF`.

A distro package places the udev rule and the systemd user unit where they
belong (`/usr/lib/udev/rules.d/`, `/usr/lib/systemd/user/`). For a manual
prefix install, copy them yourself from `data/`:

```sh
sudo cp data/70-flydigi-cooler.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
mkdir -p ~/.config/systemd/user && cp data/flykoolerd.service ~/.config/systemd/user/
systemctl --user enable --now flykoolerd   # the ExecStart path may need adjusting
```

## CLI

```sh
flykooler cli status              # state
flykooler cli set 2200            # hold 2200 rpm
flykooler cli stop                # stop the fan
flykooler cli auto                # give control back to the button
flykooler cli gear 3              # the "strong" gear
flykooler cli light color 00E0B8 40   # colour and brightness
flykooler cli light effect 5      # a built-in effect
flykooler cli standby 2           # sleep a minute after the link drops
flykooler cli sensors             # temperature sensors
```

## Releasing

1. Move the `[Unreleased]` entries in [CHANGELOG.md](CHANGELOG.md) under a
   new `## [X.Y.Z] - date` heading, and leave the file uncommitted — the
   release commit picks it up.
2. `make release VERSION=X.Y.Z`

The target refuses to run without a changelog entry, outside `main`, with a
dirty tree (only `CHANGELOG.md` may be modified) or with a malformed version.
It then runs the tests, bumps `CMakeLists.txt` and `PKGBUILD`, commits, tags,
pushes the tag and opens a GitHub release with generated notes.
Versions follow [SemVer](https://semver.org/); CI builds and tests every tag.

## Hardware notes

- The cooler has one USB-C socket: a cable into the laptop means data plus
  weak power (2700 rpm ceiling); a ≥9 V PD adapter means full speed, but
  then control works over Bluetooth only.
- While a data cable is plugged in, the cooler's BLE stays off — that is
  normal.
- The rpm ceiling is enforced by the firmware itself per supply level.

## Credits

The BS3 Pro command table and firmware notes come from
[flydigictl](https://github.com/ElXreno/flydigictl) (ElXreno) and
[THRM](https://github.com/TIANLI0/THRM) (TIANLI0).

## License

MIT — see [LICENSE](LICENSE).
