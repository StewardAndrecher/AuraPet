#!/bin/bash
set -euo pipefail

# Assembly only. The arguments point to the verified build and local upstream artifacts.
if [[ $# -ne 7 ]]; then
    printf '%s\n' 'usage: package-macos.sh BUILT_APP DEPENDENCY_APP VOICE_RUNTIME MODEL_DIR QT_PREFIX LICENSE_DIR OUTPUT_DIR' >&2
    exit 2
fi
built="$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
dependencies="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
runtime="$(cd "$3" && pwd -P)"
models="$(cd "$4" && pwd -P)"
qt="$(cd "$5" && pwd -P)"
licenses="$(cd "$6" && pwd -P)"
mkdir -p "$7"
output="$(cd "$7" && pwd -P)"
source="$(cd "$(dirname "$0")/.." && pwd -P)"
dist="$source/distribution"
version=1.0.27
edition_light="AuraPet-$version-macOS-arm64-Light"
edition_full="AuraPet-$version-macOS-arm64-Qwen-Offline"
work="$(mktemp -d "$output/.assembly.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
[[ ! -e "$output/$edition_light.zip" && ! -e "$output/$edition_full.zip" ]]

stage="$work/$edition_light"
app="$stage/AuraPet.app"
mkdir -p "$app/Contents" "$stage/licenses" "$stage/voice/runtime/bin" "$stage/voice/runtime/lib"
[[ "$(/usr/bin/plutil -extract CFBundleIconFile raw "$built/Contents/Info.plist")" = Glow.icns ]]
/usr/bin/ditto "$built/Contents" "$app/Contents"
/usr/bin/ditto "$dependencies/Contents/Frameworks" "$app/Contents/Frameworks"
/usr/bin/ditto "$dependencies/Contents/PlugIns" "$app/Contents/PlugIns"
executable="$app/Contents/MacOS/AuraPet"
xcrun install_name_tool -change /opt/homebrew/opt/libavif/lib/libavif.16.dylib @rpath/libavif.16.dylib "$executable"
while IFS= read -r rpath; do
    if [[ "$rpath" != @executable_path/../Frameworks ]]; then
        xcrun install_name_tool -delete_rpath "$rpath" "$executable"
    fi
done < <(/usr/bin/otool -l "$executable" | awk '/cmd LC_RPATH/{getline;getline;print $2}')
/usr/bin/codesign --force --sign - "$app"
/usr/bin/codesign --verify --deep --strict "$app"

/bin/cp -p "$dist/安装 AuraPet.command" "$dist/一键安装千问.command" "$dist/开始使用.md" "$stage/"
/bin/cp "$source/voice/server.py" "$source/voice/metal_backend.py" "$source/voice/README.md" "$source/voice/voices.example.json" "$dist/models.sha256" "$stage/voice/"
/bin/cp "$dist/第三方组件.md" "$licenses/"*.txt "$qt/sbom/qtbase-6.12.0.spdx.json" "$stage/licenses/"

portable="$stage/voice/runtime"
pyhome="$runtime/python"
site="$runtime/venv/lib/python3.12/site-packages"
cp -p "$pyhome/bin/python3.12" "$portable/bin/"
cp -p "$pyhome/lib/libpython3.12.dylib" "$portable/lib/"
/usr/bin/rsync -a --exclude __pycache__ --exclude site-packages --exclude ensurepip --exclude tkinter \
    --exclude 'test' --exclude 'tests' --exclude '_tkinter*' --exclude 'config-*' \
    "$pyhome/lib/python3.12" "$portable/lib/"
mkdir -p "$portable/lib/python3.12/site-packages"
for component in numpy numpy-2.5.5.dist-info qwentts_cpp qwentts_cpp_python-0.5.0.dist-info; do
    /usr/bin/rsync -a --exclude __pycache__ --exclude tests "$site/$component" "$portable/lib/python3.12/site-packages/"
done
"$portable/bin/python3.12" -I -B -c 'import numpy, qwentts_cpp, sqlite3; assert numpy.__version__ == "2.5.5"'

/usr/bin/ditto "$stage" "$work/$edition_full"
mkdir -p "$work/$edition_full/voice/gguf"
while IFS=' ' read -r hash file url; do
    actual="$(/usr/bin/shasum -a 256 "$models/$file")"
    [[ "${actual%% *}" = "$hash" ]]
    /bin/cp -c "$models/$file" "$work/$edition_full/voice/gguf/$file" 2>/dev/null ||
        /bin/cp "$models/$file" "$work/$edition_full/voice/gguf/$file"
done < "$dist/models.sha256"
/usr/bin/ditto -c -k --norsrc --noextattr --noqtn --keepParent "$stage" "$work/$edition_light.zip"
/usr/bin/ditto -c -k --norsrc --noextattr --noqtn --keepParent "$work/$edition_full" "$work/$edition_full.zip"
/usr/bin/unzip -tq "$work/$edition_light.zip"
/usr/bin/unzip -tq "$work/$edition_full.zip"
mv "$work/$edition_light.zip" "$work/$edition_full.zip" "$output/"
(
    cd "$output"
    /usr/bin/shasum -a 256 "$edition_light.zip" "$edition_full.zip" > "AuraPet-$version-SHA256SUMS.txt"
)
