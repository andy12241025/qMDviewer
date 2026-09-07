#!/usr/bin/env bash
#
# Cross-compile qMDviewer for 64-bit Windows and pack the executable together
# with every DLL and Qt plugin it needs into a zip that runs on a clean
# Windows 10/11 machine (no Qt installation, no MSVC redistributable).
#
# Requires: mingw-w64, cmake, zip, a host Qt 6 of the *same version* as the
# Windows Qt (for moc), and a Qt for Windows MinGW build, e.g. from
#   aqt install-qt windows desktop 6.4.2 win64_mingw --archives qtbase
#
# Usage: packaging/make-windows-zip.sh [/path/to/Qt/6.4.2/mingw_64]

set -euo pipefail

REPO_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$REPO_DIR"

QT_WIN=${1:-${QT_WIN:-$HOME/.cache/qt-win/6.4.2/mingw_64}}
QT_HOST=${QT_HOST:-/usr}
QT_HOST_CMAKE=${QT_HOST_CMAKE:-/usr/lib/x86_64-linux-gnu/cmake}
BUILD_DIR=${BUILD_DIR:-build-win}
TARGET=x86_64-w64-mingw32
OBJDUMP=$TARGET-objdump

VERSION=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9.]\+\).*/\1/p' CMakeLists.txt | head -1)
NAME=qMDviewer-${VERSION}-windows-x64
DIST_DIR=${DIST_DIR:-dist}
STAGE=$BUILD_DIR/package/$NAME

if [[ ! -d $QT_WIN/bin ]]; then
    echo "error: no Qt for Windows at $QT_WIN" >&2
    exit 1
fi

# Directories holding redistributable DLLs. The MinGW runtime must come from
# the same threading model the toolchain file selects (posix).
DLL_DIRS=(
    "$QT_WIN/bin"
    /usr/lib/gcc/$TARGET/*-posix
    /usr/$TARGET/lib
    /usr/$TARGET/bin
)

echo "==> Configuring ($QT_WIN)"
# Always start from a clean cache: CMake ignores CMAKE_TOOLCHAIN_FILE when a
# cache already exists, which would silently produce a host build.
rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_WIN" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DQT_HOST_PATH_CMAKE_DIR="$QT_HOST_CMAKE" >/dev/null

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)" >/dev/null
$TARGET-strip "$BUILD_DIR/qmdviewer.exe"

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD_DIR/qmdviewer.exe" "$STAGE/"

find_dll() {
    local name=$1 dir
    for dir in "${DLL_DIRS[@]}"; do
        [[ -f $dir/$name ]] && { printf '%s\n' "$dir/$name"; return 0; }
    done
    return 1
}

declare -A seen=()
declare -a system_dlls=()

# Walk the PE import tables so nothing is missed and nothing redundant is
# shipped. Anything not found in DLL_DIRS is part of Windows itself.
copy_dependencies() {
    local file=$1 name key src
    while read -r name; do
        [[ -z $name ]] && continue
        key=${name,,}
        [[ -n ${seen[$key]:-} ]] && continue
        seen[$key]=1
        if src=$(find_dll "$name"); then
            cp -f "$src" "$STAGE/"
            # Distro MinGW runtime DLLs carry debug symbols (libstdc++ alone is
            # 26 MB unstripped).
            $TARGET-strip --strip-unneeded "$STAGE/$name" 2>/dev/null || true
            copy_dependencies "$src"
        else
            system_dlls+=("$name")
        fi
    done < <($OBJDUMP -p "$file" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p')
}

echo "==> Resolving dependencies"
copy_dependencies "$STAGE/qmdviewer.exe"

# Plugins are loaded at runtime, so they are not in any import table. Qt looks
# for them next to the executable, which is the windeployqt layout.
copy_plugin() {
    local sub=$1 name=$2
    [[ -f $QT_WIN/plugins/$sub/$name ]] || return 0
    mkdir -p "$STAGE/$sub"
    cp -f "$QT_WIN/plugins/$sub/$name" "$STAGE/$sub/"
    copy_dependencies "$STAGE/$sub/$name"
}

copy_plugin platforms qwindows.dll      # required: the Windows window system
copy_plugin styles qwindowsvistastyle.dll
copy_plugin styles qmodernwindowsstyle.dll
copy_plugin imageformats qgif.dll
copy_plugin imageformats qico.dll
copy_plugin imageformats qjpeg.dll
copy_plugin imageformats qsvg.dll

cp sample.md "$STAGE/"
sed 's/$/\r/' packaging/README-windows.txt > "$STAGE/README.txt"

echo "==> Packaging"
mkdir -p "$DIST_DIR"
ZIP=$(cd "$DIST_DIR" && pwd)/$NAME.zip
rm -f "$ZIP"
( cd "$BUILD_DIR/package" && zip -qr "$ZIP" "$NAME" )

echo
echo "Windows system DLLs expected on the target: ${system_dlls[*]}"
echo
find "$STAGE" -type f | sed "s|$STAGE/||" | sort | sed 's/^/  /'
echo
echo "Package: $DIST_DIR/$NAME.zip ($(du -h "$ZIP" | cut -f1))"
