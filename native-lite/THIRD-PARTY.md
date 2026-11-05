# Third-party notices

This lightweight edition uses the existing project's GPL-3.0-or-later licensing baseline. The complete application source and build instructions are in `native-lite/`; the bundled libiec61850 source is in `third_party/libiec61850/` of the parent repository.

- **libiec61850 1.6.1**, Michael Zillgith / MZ Automation: GPL-3.0-or-later. Source: https://github.com/mz-automation/libiec61850 . The actual source used is the repository's vendored copy, not necessarily identical to an upstream release.
- **Qt 5.15.2 Core, Gui, Widgets and platform plugins**, The Qt Company and contributors: dynamically linked open-source Qt, LGPL-3.0 / GPL alternatives and component-specific third-party notices. Source: https://download.qt.io/archive/qt/5.15/5.15.2/single/ . Qt DLLs remain replaceable. Full Qt source includes its third-party copyright and license notices.
- **MinGW-w64 / GCC 8.1 runtime**: libgcc, libstdc++, libwinpthread. GCC GPL with runtime library exception where applicable; MinGW and winpthreads notices are included in this directory.

No Npcap/WinPcap binaries are distributed or required. No browser engine is distributed. Preserve these notices, corresponding source, and licenses when redistributing a release.
