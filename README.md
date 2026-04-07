# OpenSSAM

English | [한국어](README.ko.md)

OpenSSAM is a community-maintained revival of the archived SSAM 3.0 codebase for surrogate safety analysis of microsimulation trajectories.

This repository preserves the original source, binaries, and documentation while creating a path toward a modernized UI, improved algorithms, and faster analysis workflows.

The repository is intentionally split into two working areas:

- `archive/` keeps the restored historical distribution intact
- `modern/` is the active OpenSSAM workspace for UI and engine development

## Project Direction

- Preserve the historical SSAM 3.0 codebase and documentation in a public repository
- Replace the legacy desktop UI with a more maintainable interface
- Improve core analysis and parallel execution for better performance
- Make the project easier to build, document, and extend

## Repository Layout

- `archive/` - preserved archive of the restored SSAM distribution
- `archive/SSAM3` - original MFC GUI source and Visual Studio solution
- `archive/SSAMDLL` - original analysis engine, DLL, CLI source, headers, and libraries
- `archive/SSAM3EXE`, `archive/SSAM3EXE_x86` - legacy GUI binaries and dependencies
- `archive/SSAMEXE` - legacy command-line binary
- `archive/SSAM3Setup`, `archive/SSAM3Setup_x86` - legacy installers
- `archive/Documentation` - archived manuals, API docs, data dictionary, and file format specs
- `modern/` - active OpenSSAM source workspace
- `modern/SSAM3` - OpenSSAM GUI source tree for ongoing UI work
- `modern/SSAMDLL` - OpenSSAM engine/DLL source tree for algorithm and performance work
- `modern/Documentation` - documentation copied into the OpenSSAM workspace for reference

## Quick Start

End users should download the packaged `SSAM3EXE` release from GitHub Releases when available.

The `archive/` tree is kept in this repository for reference, preservation, and source comparison. It is not the primary end-user download path.

If you only need the legacy desktop application from the preserved archive, check:

- `archive/SSAM3EXE/`
- `archive/SSAM3EXE_x86/`

For command-line use:

```powershell
cd .\archive\SSAMEXE
.\SSAM_MT.exe trjfiles="C:\data\input.trj" csvfile="C:\data\output.csv" TTC=1.5 PET=5.0
```

In multithreaded builds, `nthreads=<N>` is also available.

## Build Notes

- Platform: Windows
- IDE: Visual Studio 2012
- GUI dependency: OpenSceneGraph
- Legacy binaries may require the Visual C++ Redistributable for Visual Studio 2012
- Archived GUI solution: `archive/SSAM3/SSAMAPP.sln`
- Archived engine / CLI solution: `archive/SSAMDLL/SSAMDLL.sln`
- Active GUI solution: `modern/SSAM3/SSAMAPP.sln`
- Active engine / CLI solution: `modern/SSAMDLL/SSAMDLL.sln`

## 1.0 UI Updates

The initial 1.0 portable release includes a small but practical set of GUI improvements:

- resizable main window layout
- tab area and bottom action buttons now reposition when the main window is resized
- `Conflicts` and `Summary` list views resize more cleanly and refresh column widths
- `Map` viewer placement is more stable and follows the available area better

This is still an incremental update on top of the legacy UI. A broader UI redesign remains part of the long-term roadmap.

## Roadmap

- Verify and document a reproducible build
- Separate archive artifacts from actively maintained source where appropriate
- Redesign the UI
- Profile and optimize conflict detection and motion prediction paths
- Improve large-input and multithreaded analysis performance
- Add sample data, tests, and modern developer documentation

## Archive Note

This project was initiated due to the loss of access to the original OSADP/SSAM repository.

The `archive/` tree is based on a restored copy of the former open-source SSAM 3.0 distribution. It is preserved in-repository so the code and documentation remain accessible while active OpenSSAM development continues in `modern/`.

## License

Core source files in this repository are provided under `AGPL-3.0-or-later`. See the root `LICENSE` file. Historical third-party binaries, fonts, installers, and bundled dependencies may be subject to separate licenses.

## Maintained By

OpenSSAM is maintained as a non-commercial open-source effort by [S9 Systems](https://www.s9systems.kr/).

- Website: https://www.s9systems.kr/
- Contact: shj@s9systems.kr

## Maintainer Note

This project is being developed with substantial AI assistance. Feedback is especially helpful when it includes concrete reasoning, implementation details, and step-by-step guidance.

## Personal Note

For Raily, with love.

## Contributing

Issues and pull requests are welcome, especially for:

- build notes and environment fixes
- documentation cleanup
- dependency provenance and license review
- performance profiling and optimization
- UI modernization
