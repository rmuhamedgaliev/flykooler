# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
the versioning is [SemVer](https://semver.org/).

## [Unreleased]

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

[Unreleased]: https://github.com/rmuhamedgaliev/flykooler/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/rmuhamedgaliev/flykooler/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/rmuhamedgaliev/flykooler/commits/v0.1.0
