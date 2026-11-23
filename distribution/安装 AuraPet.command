#!/bin/bash
set -euo pipefail

bundle="$(cd "$(dirname "$0")" && pwd -P)"
payload="$bundle/voice"
version=1.0.27
ulimit -c 0
mode=choose
launch=true
test_root=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --app-only) mode=app ;;
        --with-voice) mode=both ;;
        --voice-only) mode=voice ;;
        --no-launch) launch=false ;;
        --test-root) shift; test_root="${1:?需要独立测试目录}" ;;
        *) printf '%s\n' "未知选项 $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(uname -m)" != arm64 ]]; then
    printf '%s\n' '此安装包需要 Apple Silicon，并在原生 arm64 环境运行。' >&2
    exit 1
fi
IFS=. read -r major minor revision <<<"$(/usr/bin/sw_vers -productVersion)"
if (( major < 26 || (major == 26 && ${minor:-0} < 6) )); then
    printf '%s\n' '此 AuraPet 构建需要 macOS 26.6 或更新版本。' >&2
    exit 1
fi
applications="$HOME/Applications"
support="$HOME/Library/Application Support/Gif-Pet/Gif-Pet"
agents="$HOME/Library/LaunchAgents"
if [[ -n "$test_root" ]]; then
    mkdir -p "$test_root"
    test_root="$(cd "$test_root" && pwd -P)"
    [[ "$test_root" != / && "$test_root" != "$HOME" ]]
    applications="$test_root/Applications"
    support="$test_root/Data"
    launch=false
fi

if [[ "$mode" = choose ]]; then
    prompt='安装 AuraPet。可按需选择同时安装本机千问语音。'
    if [[ -d "$payload/gguf" ]]; then
        prompt='全量包已附带千问权重。选择同时安装可离线完成。'
    fi
    # Cancelling the dialog leaves the app and the voice service untouched.
    if ! answer="$(/usr/bin/osascript - "$prompt" <<'APPLESCRIPT'
on run args
    display dialog item 1 of args with title "安装 AuraPet" buttons {"取消", "仅安装 AuraPet", "同时安装千问"} default button "仅安装 AuraPet" cancel button "取消"
    return button returned of result
end run
APPLESCRIPT
)"; then exit 0; fi
    case "$answer" in
        '仅安装 AuraPet') mode=app ;;
        '同时安装千问') mode=both ;;
        *) exit 0 ;;
    esac
fi

stage=
cleanup() {
    if [[ -n "$stage" && -d "$stage" ]]; then /bin/rm -rf -- "$stage"; fi
}
trap cleanup EXIT

install_app() {
    local source target old
    source="$bundle/AuraPet.app"
    target="$applications/AuraPet.app"
    /usr/bin/codesign --verify --deep --strict "$source"
    mkdir -p "$applications"
    stage="$(mktemp -d "$applications/.AuraPet-install.XXXXXX")"
    /usr/bin/ditto "$source" "$stage/AuraPet.app"
    /usr/bin/codesign --verify --deep --strict "$stage/AuraPet.app"
    if [[ -e "$target" || -L "$target" ]]; then
        if /usr/bin/cmp -s "$source/Contents/MacOS/AuraPet" "$target/Contents/MacOS/AuraPet" &&
           /usr/bin/codesign --verify --deep --strict "$target"; then
            printf '%s\n' "应用已安装到 $target"
            cleanup
            stage=
            return
        fi
        old="$(mktemp -d "$applications/.AuraPet-backup.XXXXXX")"
        mv "$target" "$old/AuraPet.app"
        if ! mv "$stage/AuraPet.app" "$target"; then
            mv "$old/AuraPet.app" "$target"
            return 1
        fi
        printf '%s\n' "上一版备份在 $old"
    else
        mv "$stage/AuraPet.app" "$target"
    fi
    cleanup
    stage=
    printf '%s\n' "应用已安装到 $target"
}

verify_file() {
    local actual
    [[ -f "$1" ]] || return 1
    actual="$(/usr/bin/shasum -a 256 "$1")"
    [[ "${actual%% *}" = "$2" ]]
}

voice_health() {
    local health
    health="$(/usr/bin/curl --noproxy '*' -fsS --max-time 2 http://127.0.0.1:17860/health)" || return 1
    [[ "$health" == *'"ready": true'* && "$health" == *'"sample_rate": 24000'* ]]
}

install_voice() {
    local data destination hash filename url agent_file domain source prefix_version
    data="$support/voice"
    destination="$data/aurapet-managed/$version"
    prefix_version="$data/aurapet-managed"
    agent_file="$agents/com.aurapet.qwen-tts.plist"
    domain="gui/$(id -u)"
    if [[ -z "$test_root" ]] && voice_health 2>/dev/null; then
        printf '%s\n' '已检测到可用的本机千问服务，直接复用现有服务。'
        return
    fi
    if [[ -z "$test_root" && -f "$agents/com.gifpet.qwen-tts.plist" ]]; then
        /bin/launchctl kickstart "$domain/com.gifpet.qwen-tts" 2>/dev/null ||
            /bin/launchctl bootstrap "$domain" "$agents/com.gifpet.qwen-tts.plist" 2>/dev/null || true
        for try in {1..12}; do
            if voice_health 2>/dev/null; then
                printf '%s\n' '复用了现有千问服务和音色配置。'
                return
            fi
            sleep 1
        done
        printf '%s\n' '已有千问服务配置且暂未就绪。请先检查原有语音服务，AuraPet 已保留该配置。' >&2
        return 1
    fi
    mkdir -p "$prefix_version" "$data/logs"
    stage="$(mktemp -d "$prefix_version/.stage.XXXXXX")"
    mkdir -p "$stage/gguf"
    /usr/bin/ditto "$payload/runtime" "$stage/runtime"
    /bin/cp "$payload/server.py" "$payload/metal_backend.py" "$stage/"
    while IFS=' ' read -r hash filename url; do
        [[ -n "$filename" ]] || continue
        source="$payload/gguf/$filename"
        if [[ -f "$source" ]]; then
            printf '%s\n' "正在校验随包模型 $filename"
            verify_file "$source" "$hash"
            /bin/cp -c "$source" "$stage/gguf/$filename" 2>/dev/null ||
                /bin/cp "$source" "$stage/gguf/$filename"
        elif verify_file "$destination/gguf/$filename" "$hash"; then
            /bin/cp -c "$destination/gguf/$filename" "$stage/gguf/$filename" 2>/dev/null ||
                /bin/cp "$destination/gguf/$filename" "$stage/gguf/$filename"
        else
            printf '%s\n' "正在从 Hugging Face 下载 $filename"
            /usr/bin/curl --proto '=https' --proto-redir '=https' --fail --location --retry 2 --connect-timeout 15 \
                --output "$stage/gguf/$filename" "$url"
            verify_file "$stage/gguf/$filename" "$hash"
        fi
    done < "$payload/models.sha256"
    "$stage/runtime/bin/python3.12" -I -B -c 'import numpy, qwentts_cpp'
    [[ -s "$stage/gguf/qwen-talker-0.6b-base-Q8_0.gguf" && -s "$stage/gguf/qwen-tokenizer-12hz-Q8_0.gguf" ]]
    local backup=
    if [[ -e "$destination" ]]; then
        backup="$(mktemp -d "$prefix_version/.old.XXXXXX")"
        mv "$destination" "$backup/$version"
    fi
    if ! mv "$stage" "$destination"; then
        if [[ -n "$backup" ]]; then mv "$backup/$version" "$destination"; fi
        return 1
    fi
    stage=
    if [[ -z "$test_root" ]]; then
        mkdir -p "$agents"
        if [[ -f "$agent_file" ]]; then
            /bin/launchctl bootout "$domain/com.aurapet.qwen-tts" 2>/dev/null || true
        fi
        "$destination/runtime/bin/python3.12" -I -B - "$agent_file" "$destination" "$data" "$support" <<'PY'
from pathlib import Path
import plistlib
import sys
file, base, data, support = map(Path, sys.argv[1:])
document = {
    "Label": "com.aurapet.qwen-tts",
    "ProgramArguments": [
        str(base / "runtime/bin/python3.12"), "-E", "-s", "-B", str(base / "server.py"),
        "--backend", "metal", "--voices", str(data / "voices.json"), "--role-database", str(support / "roles.sqlite3"),
    ],
    "RunAtLoad": True,
    "StandardOutPath": str(data / "logs/aurapet-qwen.log"),
    "StandardErrorPath": str(data / "logs/aurapet-qwen.error.log"),
}
temp = file.with_suffix(".tmp")
temp.write_bytes(plistlib.dumps(document))
temp.chmod(0o644)
temp.replace(file)
PY
        /bin/launchctl bootstrap "$domain" "$agent_file"
        local ready=false
        for try in {1..35}; do
            if voice_health 2>/dev/null; then ready=true; break; fi
            sleep 1
        done
        if [[ "$ready" != true ]]; then
            printf '%s\n' "千问已安装，但服务暂未就绪。请查看 $data/logs/aurapet-qwen.error.log" >&2
            return 1
        fi
    fi
    printf '%s\n' "本机千问已安装到 $destination"
}

if [[ "$mode" != voice ]]; then install_app; fi
if [[ "$mode" != app ]]; then install_voice; fi
if [[ "$mode" != voice && "$launch" = true ]]; then /usr/bin/open "$applications/AuraPet.app"; fi
