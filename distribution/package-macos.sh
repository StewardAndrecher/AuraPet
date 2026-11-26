#!/bin/bash
set -euo pipefail

# Packaging script for macOS arm64.
# Mode 1 (CI / Light only):
#   package-macos.sh --ci BUILT_APP QT_PREFIX OUTPUT_DIR
# Mode 2 (Light + Qwen Offline with local GGUF models):
#   package-macos.sh --full-with-models BUILT_APP MODEL_DIR QT_PREFIX OUTPUT_DIR
# Mode 3 (Full with portable voice runtime & offline models):
#   package-macos.sh BUILT_APP DEPENDENCY_APP VOICE_RUNTIME MODEL_DIR QT_PREFIX LICENSE_DIR OUTPUT_DIR

source="$(cd "$(dirname "$0")/.." && pwd -P)"
dist="$source/distribution"
version="$(tr -d '[:space:]' < "$source/VERSION.txt")"
sign_identity="${CODESIGN_IDENTITY:--}"
notary_profile="${NOTARY_PROFILE:-}"

sign_app() {
    local app_path="$1"
    if [[ "$sign_identity" == "-" ]]; then
        printf '%s\n' "Info: Using ad-hoc codesign (set CODESIGN_IDENTITY for Developer ID signing)."
        /usr/bin/codesign --force --deep --sign - "$app_path"
    else
        printf '%s\n' "Info: Signing with Developer ID: $sign_identity"
        /usr/bin/codesign --force --deep --options runtime --timestamp --sign "$sign_identity" "$app_path"
    fi
    /usr/bin/codesign --verify --deep --strict "$app_path"
}

notarize_archive() {
    local zip_path="$1"
    if [[ -n "$notary_profile" && "$sign_identity" != "-" ]]; then
        printf '%s\n' "Info: Submitting $zip_path for Apple Notarization..."
        /usr/bin/xcrun notarytool submit "$zip_path" --keychain-profile "$notary_profile" --wait
    fi
}

copy_common_docs() {
    local stage_dir="$1"
    /bin/cp -p "$source/VERSION.txt" "$stage_dir/VERSION.txt"
    /bin/cp -p "$dist/安装 AuraPet.command" "$dist/开始使用.md" "$stage_dir/"
    if [[ -f "$source/docs/安装文档.md" ]]; then /bin/cp -p "$source/docs/安装文档.md" "$stage_dir/"; fi
    if [[ -f "$source/docs/使用文档.md" ]]; then /bin/cp -p "$source/docs/使用文档.md" "$stage_dir/"; fi
    /bin/chmod +x "$stage_dir/安装 AuraPet.command"
    /bin/cp "$source/voice/server.py" "$source/voice/metal_backend.py" "$source/voice/README.md" \
            "$source/voice/voices.example.json" "$dist/models.sha256" "$stage_dir/voice/"
    /bin/cp "$dist/第三方组件.md" "$source/LICENSE" "$stage_dir/licenses/"
}

if [[ "${1:-}" == "--ci" ]]; then
    if [[ $# -ne 4 ]]; then
        printf '%s\n' 'usage: package-macos.sh --ci BUILT_APP QT_PREFIX OUTPUT_DIR' >&2
        exit 2
    fi
    built="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
    qt="$(cd "$3" && pwd -P)"
    mkdir -p "$4"
    output="$(cd "$4" && pwd -P)"
    edition_light="AuraPet-$version-macOS-arm64-Light"
    work="$(mktemp -d "$output/.assembly.XXXXXX")"
    trap 'rm -rf -- "$work"' EXIT

    stage="$work/$edition_light"
    app="$stage/AuraPet.app"
    mkdir -p "$app" "$stage/licenses" "$stage/voice"
    /usr/bin/ditto "$built" "$app"

    "$qt/bin/macdeployqt" "$app" -no-strip -always-overwrite
    sign_app "$app"
    copy_common_docs "$stage"

    /usr/bin/ditto -c -k --norsrc --noextattr --noqtn --keepParent "$stage" "$work/$edition_light.zip"
    /usr/bin/unzip -tq "$work/$edition_light.zip"
    notarize_archive "$work/$edition_light.zip"
    mv -f "$work/$edition_light.zip" "$output/"
    (
        cd "$output"
        /usr/bin/shasum -a 256 "$edition_light.zip" > "AuraPet-$version-macOS-SHA256SUMS.txt"
    )
    printf '%s\n' "Created $output/$edition_light.zip"
    exit 0
fi

if [[ "${1:-}" == "--full-with-models" ]]; then
    if [[ $# -ne 5 ]]; then
        printf '%s\n' 'usage: package-macos.sh --full-with-models BUILT_APP MODEL_DIR QT_PREFIX OUTPUT_DIR' >&2
        exit 2
    fi
    built="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
    models="$(cd "$3" && pwd -P)"
    qt="$(cd "$4" && pwd -P)"
    mkdir -p "$5"
    output="$(cd "$5" && pwd -P)"
    edition_light="AuraPet-$version-macOS-arm64-Light"
    edition_full="AuraPet-$version-macOS-arm64-Qwen-Offline"
    work="$(mktemp -d "$output/.assembly.XXXXXX")"
    trap 'rm -rf -- "$work"' EXIT

    stage="$work/$edition_light"
    app="$stage/AuraPet.app"
    mkdir -p "$app" "$stage/licenses" "$stage/voice"
    /usr/bin/ditto "$built" "$app"

    "$qt/bin/macdeployqt" "$app" -no-strip -always-overwrite
    sign_app "$app"
    copy_common_docs "$stage"

    /usr/bin/ditto "$stage" "$work/$edition_full"
    /bin/cp -p "$dist/一键安装千问.command" "$work/$edition_full/"
    /bin/chmod +x "$work/$edition_full/一键安装千问.command"
    mkdir -p "$work/$edition_full/voice/gguf"
    while IFS=' ' read -r hash file url; do
        [[ -n "$file" ]] || continue
        actual="$(/usr/bin/shasum -a 256 "$models/$file")"
        [[ "${actual%% *}" = "$hash" ]]
        /bin/cp -c "$models/$file" "$work/$edition_full/voice/gguf/$file" 2>/dev/null ||
            /bin/cp "$models/$file" "$work/$edition_full/voice/gguf/$file"
    done < "$dist/models.sha256"

    /usr/bin/ditto -c -k --norsrc --noextattr --noqtn --keepParent "$stage" "$work/$edition_light.zip"
    /usr/bin/ditto -c -k --norsrc --noextattr --noqtn --keepParent "$work/$edition_full" "$work/$edition_full.zip"
    /usr/bin/unzip -tq "$work/$edition_light.zip"
    /usr/bin/unzip -tq "$work/$edition_full.zip"
    notarize_archive "$work/$edition_light.zip"
    notarize_archive "$work/$edition_full.zip"
    mv -f "$work/$edition_light.zip" "$work/$edition_full.zip" "$output/"
    (
        cd "$output"
        /usr/bin/shasum -a 256 "$edition_light.zip" "$edition_full.zip" > "AuraPet-$version-macOS-SHA256SUMS.txt"
    )
    printf '%s\n' "Created $output/$edition_light.zip and $output/$edition_full.zip"
    exit 0
fi

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
xcrun install_name_tool -change /opt/homebrew/opt/libavif/lib/libavif.16.dylib @rpath/libavif.16.dylib "$executable" || true
while IFS= read -r rpath; do
    if [[ "$rpath" != @executable_path/../Frameworks ]]; then
        xcrun install_name_tool -delete_rpath "$rpath" "$executable"
    fi
done < <(/usr/bin/otool -l "$executable" | awk '/cmd LC_RPATH/{getline;getline;print $2}')
sign_app "$app"

copy_common_docs "$stage"
/bin/cp -p "$dist/一键安装千问.command" "$stage/"
/bin/chmod +x "$stage/一键安装千问.command"
/bin/cp "$licenses/"*.txt "$stage/licenses/"
if [[ -f "$qt/sbom/qtbase-6.12.0.spdx.json" ]]; then
    /bin/cp "$qt/sbom/qtbase-6.12.0.spdx.json" "$stage/licenses/"
fi

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
notarize_archive "$work/$edition_light.zip"
notarize_archive "$work/$edition_full.zip"
mv "$work/$edition_light.zip" "$work/$edition_full.zip" "$output/"
(
    cd "$output"
    /usr/bin/shasum -a 256 "$edition_light.zip" "$edition_full.zip" > "AuraPet-$version-SHA256SUMS.txt"
)
