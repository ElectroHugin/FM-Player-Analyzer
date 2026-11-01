# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/),
certificate by [SignPath Foundation](https://signpath.org/).

This policy covers the Windows release files of **FM Player Analyzer**
(`FMPlayerAnalyzer-<version>-Windows-x64-Setup.exe` and the program file
`fmplayeranalyzer.exe` it installs), published on the
[GitHub releases page](https://github.com/ElectroHugin/FM-Player-Analyzer/releases).

## What gets signed

- Only files built from the source code in this repository are signed.
- Every release is built by GitHub Actions
  ([`.github/workflows/release.yml`](.github/workflows/release.yml)) from a
  commit on `main`. Nothing is built or signed on a personal computer.
- Third-party libraries shipped with the app (Qt, the Microsoft Visual C++
  runtime) are included as published by their authors and are not signed
  with this project's certificate.
- Each signing request is approved by hand before the signature is applied.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [ElectroHugin](https://github.com/ElectroHugin) |
| Approvers | [ElectroHugin](https://github.com/ElectroHugin) |

Changes from people outside the team are only merged after a review by a
committer. All team members use multi-factor authentication for GitHub and
SignPath.

## Privacy policy

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it.

FM Player Analyzer works fully offline. It reads only the export files the
user selects and stores its data in `%LOCALAPPDATA%\FM24PlayerAnalyzer` on the
user's own computer.

## Installing and uninstalling

The installer adds the app to *Settings → Apps*, where it can be removed like
any other program. Uninstalling removes the program files; the user's own data
in `%LOCALAPPDATA%\FM24PlayerAnalyzer` is kept and can be deleted by hand.

The installer only changes something outside its own folder when the user
ticks the matching option: it can copy the export views for Football Manager
2027 into the game's `views` folder in the user's documents. These files can be
deleted there at any time.
