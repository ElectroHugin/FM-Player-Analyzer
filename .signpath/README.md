# SignPath configuration (reference copy)

These artifact configurations are entered in the SignPath web UI of the
project `FM-Player-Analyzer`. They are kept here so changes are reviewed like
code. See [CODE_SIGNING.md](../CODE_SIGNING.md) and the signing steps in
[.github/workflows/release.yml](../.github/workflows/release.yml).

| Slug | File | Signs |
|---|---|---|
| `program` | `artifact-configurations/program.xml` | `fmplayeranalyzer.exe`, before the installer is built |
| `installer` | `artifact-configurations/installer.xml` | `FMPlayerAnalyzer-<version>-Windows-x64-Setup.exe` |

Both check the product name and version stored in the file, so only this
project's release builds of the expected version can be signed.
