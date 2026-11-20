# Developing with Qt Creator

The project is a **CMake project**: the project file for Qt Creator is
[`CMakeLists.txt`](CMakeLists.txt) in this folder. CMake projects have no
separate `.pro` file.

## Open the project

1. Start Qt Creator
2. **File → Open File or Project…** (or "Open Project" on the welcome page)
3. Select `desktop/CMakeLists.txt`
4. On first opening, Qt Creator asks for the configuration:
   - Qt Creator detects our **`CMakePresets.json`** automatically: choose the
     preset **`msvc-debug`** (for developing/debugging) or **`msvc-release`**
     (full optimisation, for performance measurements).
   - Alternatively choose a kit with **Qt 6.8.3 MSVC2022 64bit**
     (`C:\Qt\6.8.3\msvc2022_64`) and the **MSVC 2022 x64** compiler.

## Build and run

- **Build:** Ctrl+B (hammer icon)
- **Run:** Ctrl+R; select **`fmplayeranalyzer`** as run target (bottom left,
  via the monitor icon). Qt Creator sets the Qt `PATH` automatically on start,
  `windeployqt` is not needed.
- Other targets: `fmmigrate` (legacy DB migration on the command line),
  `fmgolden` (golden master comparison against the Python reference).

## Run tests

- **Tools → Tests → Run All Tests**, or in a terminal:
  ```
  ctest --preset msvc-debug
  ```
- Qt Creator lists the Qt Test suites (`test_utils`, `test_dwrs`, …) one by one
  in the "Test Results" output pane.

## Notes

- Build folders live under `desktop/build/<preset>/` and are git-ignored.
- Qt Creator stores local configurations in `CMakeUserPresets.json` or
  `CMakeLists.txt.user`; both are (or will be) git-ignored and do not belong in
  the repository.
- Command-line build without Qt Creator:
  `.\scripts\build.ps1 -Preset msvc-release -Test`
