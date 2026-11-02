# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
the versioning is [SemVer](https://semver.org/).

## [Unreleased]

## [0.2.0] - 2026-11-02

### Added
- Power state in the window: a green/red badge shows whether the cooler is
  running or off, and *Turn on* / *Turn off* buttons (also in the tray menu)
  wake it or put it to sleep by hand
- The cooler follows the host: on suspend or shutdown the backend stops the
  fan, blanks the light and puts it to sleep, and disables the cooler's own
  auto-start so power alone does not switch it on. It is woken again on
  resume or the next connection. Controlled by "Turn the cooler off when the
  laptop sleeps or shuts down", `flykooler cli host on|off`, and the new
  `off` / `wake` CLI commands
- logind's `PrepareForSleep` / `PrepareForShutdown` signals are used when
  Qt DBus is available; `SIGTERM` / `SIGINT` / `SIGHUP` cover logout and
  service stop

## [0.1.2] - 2026-11-01

### Fixed
- A second backend no longer unlinks a live socket and drives the cooler in
  parallel: it takes a lock and probes the socket before listening
- The installed GUI starts the systemd unit instead of spawning a backend
  outside its supervision

### Changed
- `make release` takes the GitHub release notes from the version's CHANGELOG
  section; generated notes are usually empty in a repository without PRs

## [0.1.1] - 2026-11-01

### Added
- `make release VERSION=X.Y.Z`: refuses to run without a changelog entry,
  outside `main`, with a dirty tree or with a malformed version, then tests,
  bumps `CMakeLists.txt` and `PKGBUILD`, commits, tags, pushes and opens the
  GitHub release

### Changed
- README lists which coolers are actually tested instead of implying support
  for every BS-series model

### Fixed
- BS1 (`37d7:1001`) is ignored by the hidraw driver: it speaks BLE/GATT
  rather than HID, so it would only have received frames it cannot parse

## [0.1.0] - 2026-10-31

Initial release.

### Added
- `flykoolerd` backend: hidraw driver over USB and Bluetooth with hot
  reconnect, BS-series wire protocol, temperature fan curve, lighting
  upload, systemd user service, JSON API over a Unix socket
- `flykooler` GUI: Plasma tray, three fan modes, curve and gear editors,
  lighting controls; ru_RU UI translation loaded by system locale
- CLI: `status / set / stop / auto / gear / light / standby / sensors`
- Unit tests against frames captured from real hardware; ASan/UBSan clean
- Packaging: PKGBUILD (Arch), CPack (TGZ/DEB/RPM), udev rule, systemd
  user unit; GitHub Actions CI

[Unreleased]: https://github.com/rmuhamedgaliev/flykooler/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/rmuhamedgaliev/flykooler/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/rmuhamedgaliev/flykooler/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/rmuhamedgaliev/flykooler/commits/v0.1.0
