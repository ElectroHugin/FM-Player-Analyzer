#!/usr/bin/env bash
# Starts the AppImage from build/appimage once with --smoke and checks that it
# comes up on its own: on the xcb platform, with a throwaway home directory and
# without the build machine's Qt in the environment.
# Usage: scripts/smoke-appimage.sh        (uses xvfb-run when it is installed)
set -euo pipefail

desktop_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
appimages=("$desktop_dir"/build/appimage/*.AppImage)
[ -f "${appimages[0]}" ] || { echo "smoke-appimage: no AppImage in build/appimage" >&2; exit 1; }
appimage="${appimages[0]}"

home="$(mktemp -d)"
trap 'rm -rf "$home"' EXIT

# A bootstrap.ini with a data folder skips the first-run dialog.
config_dir="$home/.config/FM24PlayerAnalyzer"
mkdir -p "$config_dir"
printf '[General]\ndataDir=%s\n' "$home/data" > "$config_dir/bootstrap.ini"

runner=()
if command -v xvfb-run > /dev/null; then
    runner=(xvfb-run -a)
fi

# A failed start ends in a message box that nobody closes: the timeout turns
# that into an error.
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QT_ROOT_DIR \
    HOME="$home" XDG_CONFIG_HOME="$home/.config" \
    QT_QPA_PLATFORM=xcb APPIMAGE_EXTRACT_AND_RUN=1 \
    timeout 120 "${runner[@]}" "$appimage" --smoke

# The app created its database, so the bundled SQLite driver was loaded.
databases=("$home"/data/databases/*.db)
[ -f "${databases[0]}" ] || { echo "smoke-appimage: the app did not create a database" >&2; exit 1; }
echo "Smoke test passed: $(basename "$appimage")"
