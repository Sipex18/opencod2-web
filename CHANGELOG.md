# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog and this project follows a simple date-based release workflow.

Automation helper:
- `powershell -ExecutionPolicy Bypass -File tools/changelog_add.ps1 -Type Fixed -Message "your change"`

## [Unreleased]

### Added
### Changed
- Added automated changelog entry helper script at tools/changelog_add.ps1.
### Deprecated
### Removed

### Fixed
- Fixed varargs type mismatches in src/PC/server_mp/sv_net_chan_mp.c by passing int values to %i format slots in Com_sprintf network profile lines.
- Aligned NetProf_PrepProfiling declaration/cast in src/PC/server_mp/sv_net_chan_mp.c to use netProfileInfo_t**.
- `src/PC/qcommon/net_chan_mp.c`: fixed incorrect `Com_Printf` format specifier in `NetProf_PrepProfiling` (`%s` -> `%i`) for `g_qport`.

### Security

## [2026-06-28]

### Added
- Started maintaining a project changelog.
