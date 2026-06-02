# Changelog

All notable changes to FastScreen will be documented in this file.

## [1.0.0] - 2026-04-16

### Added
- Initial release of FastScreen
- DXGI Desktop Duplication API integration for zero-copy capture
- Single screenshot capture (`captureScreen`, `captureRaw`)
- High-FPS streaming mode (`startStream`, `getNextFrame`)
- Multi-monitor support
- FastCore integration for unified JNI loading
- Maven and JitPack distribution

### Performance
- 500-2000 FPS streaming capture
- 70-275× faster than java.awt.Robot
- Zero GC pressure with reusable native buffers

### Platform
- Windows 10/11 support only (requires DXGI)

---

## Template for Future Releases

### Added
- New features

### Changed
- Changes to existing functionality

### Deprecated
- Soon-to-be removed features

### Removed
- Removed features

### Fixed
- Bug fixes

### Security
- Security improvements
