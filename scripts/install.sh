#!/bin/bash
set -euo pipefail

repo="StewardAndrecher/AuraPet"
tag="v1.0.27"
mode=--app-only
edition=Light
no_launch=false

case "${1:---light}" in
    --light) ;;
    --with-qwen) mode=--with-voice ;;
    --offline-package) mode=--with-voice; edition=Qwen-Offline ;;
    --help|-h)
        printf '%s\n' 'install.sh [--light | --with-qwen | --offline-package] [--no-launch]'
        exit 0 ;;
    *) printf '%s\n' "无法识别的选项 $1" >&2; exit 2 ;;
esac
if [[ "${2:-}" = --no-launch ]]; then
    no_launch=true
elif [[ $# -gt 1 ]]; then
    printf '%s\n' '第二个参数可选 --no-launch。' >&2
    exit 2
fi

if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    printf '%s\n' '此安装器支持 Apple Silicon Mac，请使用原生 arm64 终端。' >&2
    exit 1
fi
IFS=. read -r major minor _ <<<"$(/usr/bin/sw_vers -productVersion)"
if (( major < 26 || (major == 26 && ${minor:-0} < 6) )); then
    printf '%s\n' '需要 macOS 26.6 或更新系统。' >&2
    exit 1
fi

case "$edition" in
    Light) expected=7281ad9b60f84e9673bdeeaa31aafadec8d4bbba2868e12a619337c44557f947 ;;
    Qwen-Offline) expected=3d4a6c20ee0afd59329966e314bf94f36fe69cdaab78e8d4bd7c115b72f7db83 ;;
esac
name="AuraPet-${tag#v}-macOS-arm64-$edition"
work="$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/aurapet-install.XXXXXX")"
trap '/bin/rm -rf -- "$work"' EXIT
printf '%s\n' "下载 $name"
/usr/bin/curl --proto '=https' --proto-redir '=https' --fail --show-error --location --retry 2 \
    --connect-timeout 15 "https://github.com/$repo/releases/download/$tag/$name.zip" -o "$work/$name.zip"
printf '%s  %s\n' "$expected" "$work/$name.zip" | /usr/bin/shasum -a 256 -c -
/usr/bin/ditto -x -k "$work/$name.zip" "$work"
if [[ "$no_launch" = true ]]; then
    /bin/bash "$work/$name/安装 AuraPet.command" "$mode" --no-launch
else
    /bin/bash "$work/$name/安装 AuraPet.command" "$mode"
fi
