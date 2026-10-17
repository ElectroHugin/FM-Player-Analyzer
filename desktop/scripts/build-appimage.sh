#!/usr/bin/env bash
# Packages an existing Linux build as an AppImage.
# Usage: scripts/build-appimage.sh [preset]        (default: linux-release)
#
# Needs Qt's qmake on PATH (or QMAKE set) and curl. linuxdeploy is downloaded
# once into build/appimage/tools. Result:
#   build/appimage/FMPlayerAnalyzer-<version>-Linux-x86_64.AppImage
set -euo pipefail

preset="${1:-linux-release}"
desktop_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$desktop_dir/build/$preset/src/app/fmplayeranalyzer"
package_dir="$desktop_dir/installer/linux"
out_dir="$desktop_dir/build/appimage"
app_dir="$out_dir/AppDir"

# Pinned release, checked against its published SHA-256.
linuxdeploy_tag="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
linuxdeploy="$out_dir/tools/linuxdeploy-$linuxdeploy_tag-x86_64.AppImage"

# Qt plugins the app needs. They are copied by hand instead of using
# linuxdeploy-plugin-qt, which bundles every SQL driver of the Qt installation
# (MySQL, PostgreSQL, ODBC) together with their client libraries.
qt_plugins=(
    platforms/libqxcb.so
    xcbglintegrations/libqxcb-glx-integration.so
    xcbglintegrations/libqxcb-egl-integration.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
    platforminputcontexts/libibusplatforminputcontextplugin.so
    platformthemes/libqxdgdesktopportal.so
    imageformats/libqico.so
    imageformats/libqjpeg.so
    imageformats/libqgif.so
    sqldrivers/libqsqlite.so
)

fail() { echo "build-appimage: $*" >&2; exit 1; }

[ -x "$binary" ] || fail "app binary not found: $binary (build the preset first)"

qmake="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[ -n "$qmake" ] || fail "qmake not found (put Qt's bin directory on PATH or set QMAKE)"
qt_plugin_dir="$("$qmake" -query QT_INSTALL_PLUGINS)"
qt_lib_dir="$("$qmake" -query QT_INSTALL_LIBS)"

# Version from Version.cpp (single source of truth).
version="$(sed -nE 's/.*QStringLiteral\("([0-9]+\.[0-9]+\.[0-9]+)"\).*/\1/p' \
    "$desktop_dir/src/core/Version.cpp" | head -n 1)"
[ -n "$version" ] || fail "could not read the version from Version.cpp"
echo "Packaging version $version"

# --- 1. linuxdeploy ---
if [ ! -x "$linuxdeploy" ]; then
    mkdir -p "$(dirname "$linuxdeploy")"
    curl -fsSL -o "$linuxdeploy.part" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_tag/linuxdeploy-x86_64.AppImage"
    echo "$linuxdeploy_sha256  $linuxdeploy.part" | sha256sum --check --quiet \
        || fail "checksum mismatch for linuxdeploy $linuxdeploy_tag"
    chmod +x "$linuxdeploy.part"
    mv "$linuxdeploy.part" "$linuxdeploy"
fi

# --- 2. AppDir: Qt plugins + qt.conf ---
rm -rf "$app_dir"
rm -f "$out_dir"/*.AppImage
mkdir -p "$app_dir/usr/bin"

deps_only_args=()
for plugin in "${qt_plugins[@]}"; do
    [ -f "$qt_plugin_dir/$plugin" ] || fail "Qt plugin not found: $qt_plugin_dir/$plugin"
    install -D -m 755 "$qt_plugin_dir/$plugin" "$app_dir/usr/plugins/$plugin"
    deps_only_args+=(--deploy-deps-only "$app_dir/usr/plugins/$plugin")
done

# Qt reads qt.conf next to the executable; it points at the bundled plugins.
printf '[Paths]\nPrefix = ../\nPlugins = plugins\n' > "$app_dir/usr/bin/qt.conf"

# --- 3. Bundle libraries and build the AppImage ---
# The copied plugins resolve their Qt libraries through this path.
export LD_LIBRARY_PATH="$qt_lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Runs the linuxdeploy AppImage without FUSE (not available on CI runners).
export APPIMAGE_EXTRACT_AND_RUN=1
# linuxdeploy's bundled strip fails on libraries of newer distributions.
export NO_STRIP=1
export ARCH=x86_64
export LINUXDEPLOY_OUTPUT_VERSION="$version"

cd "$out_dir"
"$linuxdeploy" --appdir "$app_dir" \
    --executable "$binary" \
    --desktop-file "$package_dir/fmplayeranalyzer.desktop" \
    --icon-file "$package_dir/fmplayeranalyzer.png" \
    "${deps_only_args[@]}" \
    --output appimage

# linuxdeploy names the file after the desktop entry; use the release name.
target="$out_dir/FMPlayerAnalyzer-$version-Linux-x86_64.AppImage"
built=("$out_dir"/*.AppImage)
[ "${#built[@]}" -eq 1 ] && [ -f "${built[0]}" ] || fail "expected exactly one AppImage in $out_dir"
[ "${built[0]}" = "$target" ] || mv "${built[0]}" "$target"
echo "AppImage: $target"
