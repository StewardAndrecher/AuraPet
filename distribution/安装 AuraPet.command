#!/bin/bash
set -euo pipefail

bundle="$(cd "$(dirname "$0")" && pwd -P)"
payload="$bundle/voice"
version="1.0.34"
if [[ -f "$bundle/VERSION.txt" ]]; then
    version="$(tr -d '[:space:]' < "$bundle/VERSION.txt")"
fi
ulimit -c 0
mode=choose
launch=true
test_root=
hf_endpoint="${HF_ENDPOINT:-https://huggingface.co}"
hf_mirror="https://hf-mirror.com"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app-only) mode=app ;;
        --with-voice) mode=both ;;
        --voice-only) mode=voice ;;
        --no-launch) launch=false ;;
        --mirror) hf_endpoint="$hf_mirror" ;;
        --test-root) shift; test_root="${1:?需要独立测试目录}" ;;
        *) printf '%s\n' "未知选项 $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(uname -m)" != arm64 ]]; then
    printf '%s\n' '此安装包需要 Apple Silicon（M 系列芯片），并在原生 arm64 环境运行。' >&2
    exit 1
fi
IFS=. read -r major minor revision <<<"$(/usr/bin/sw_vers -productVersion)"
if (( major < 14 )); then
    printf '%s\n' '此 AuraPet 构建需要 macOS 14.0（Sonoma）或更新版本。' >&2
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
    if [[ -d "$payload/runtime" || -d "$payload/gguf" ]]; then
        prompt="安装 AuraPet $version。可按需选择同时安装本机千问语音（需 Apple Silicon）。"
        if [[ -d "$payload/gguf" ]]; then
            prompt="全量包已附带千问模型权重（AuraPet $version）。选择「同时安装千问」将离线校验模型并配置本机语音服务。"
        fi
        if ! answer="$(/usr/bin/osascript - "$prompt" <<'APPLESCRIPT'
on run args
    display dialog item 1 of args with title "安装 AuraPet" buttons {"取消", "仅安装 AuraPet", "同时安装千问"} default button "同时安装千问" cancel button "取消"
    return button returned of result
end run
APPLESCRIPT
)"; then exit 0; fi
        case "$answer" in
            '仅安装 AuraPet') mode=app ;;
            '同时安装千问') mode=both ;;
            *) exit 0 ;;
        esac
    else
        prompt="即将安装 AuraPet $version 到 ~/Applications/AuraPet.app 并自动启动（当前为无千问极简版，含文字聊天与系统语音）。"
        if ! answer="$(/usr/bin/osascript - "$prompt" <<'APPLESCRIPT'
on run args
    display dialog item 1 of args with title "安装 AuraPet" buttons {"取消", "一键安装 AuraPet"} default button "一键安装 AuraPet" cancel button "取消"
    return button returned of result
end run
APPLESCRIPT
)"; then exit 0; fi
        case "$answer" in
            '一键安装 AuraPet') mode=app ;;
            *) exit 0 ;;
        esac
    fi
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
    # Remove Gatekeeper quarantine attributes when users download directly from GitHub Releases
    /usr/bin/xattr -dr com.apple.quarantine "$source" 2>/dev/null || true
    /usr/bin/codesign --verify --deep --strict "$source"
    mkdir -p "$applications"
    stage="$(mktemp -d "$applications/.AuraPet-install.XXXXXX")"
    /usr/bin/ditto "$source" "$stage/AuraPet.app"
    /usr/bin/xattr -dr com.apple.quarantine "$stage/AuraPet.app" 2>/dev/null || true
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

download_model() {
    local url="$1"
    local dest="$2"
    local mirror_url="${url/https:\/\/huggingface.co/$hf_mirror}"
    local primary_url="${url/https:\/\/huggingface.co/$hf_endpoint}"
    printf '%s\n' "正在下载模型：$(basename "$dest")"
    if ! /usr/bin/curl --proto '=https' --proto-redir '=https' --fail --location --retry 2 --connect-timeout 15 \
        --progress-bar --output "$dest" "$primary_url"; then
        if [[ "$primary_url" != "$mirror_url" ]]; then
            printf '%s\n' "主源连接超时，自动切换镜像源重试：$hf_mirror"
            /usr/bin/curl --proto '=https' --proto-redir '=https' --fail --location --retry 2 --connect-timeout 15 \
                --progress-bar --output "$dest" "$mirror_url"
        else
            return 1
        fi
    fi
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
    if [[ ! -d "$payload/runtime" && ! -d "$payload/gguf" ]]; then
        printf '%s\n' '当前为无千问极简安装包，未内置千问语音权重。请下载「带千问全量包」，或按 voice/README.md 手动部署。' >&2
        return 1
    fi
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
    local pyrel="runtime/bin/python3.12"
    if [[ -d "$payload/runtime" ]]; then
        /usr/bin/ditto "$payload/runtime" "$stage/runtime"
    else
        local host_py=""
        for candidate in /opt/homebrew/bin/python3.12 /usr/local/bin/python3.12 "$(command -v python3.12 2>/dev/null || true)" "$(command -v python3 2>/dev/null || true)"; do
            if [[ -n "$candidate" && -x "$candidate" ]]; then
                host_py="$candidate"
                break
            fi
        done
        if [[ -z "$host_py" ]]; then
            printf '%s\n' '未检测到 Python 3，请先运行 brew install python@3.12 后再重新双击「一键安装千问.command」。' >&2
            return 1
        fi
        printf '%s\n' "正在创建独立 Python 语音虚拟环境（$host_py）..."
        "$host_py" -m venv "$stage/runtime"
        pyrel="runtime/bin/python3"
        "$stage/$pyrel" -m pip install --upgrade pip >/dev/null 2>&1 || true
        if ! "$stage/$pyrel" -m pip install numpy qwentts-cpp-python; then
            printf '%s\n' "官方源安装较慢或受限，切换清华镜像重试..."
            "$stage/$pyrel" -m pip install -i https://pypi.tuna.tsinghua.edu.cn/simple numpy qwentts-cpp-python
        fi
    fi
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
            download_model "$url" "$stage/gguf/$filename"
            verify_file "$stage/gguf/$filename" "$hash"
        fi
    done < "$payload/models.sha256"
    "$stage/$pyrel" -B -c 'import numpy, qwentts_cpp'
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
        "$destination/$pyrel" -B - "$agent_file" "$destination" "$data" "$support" "$pyrel" <<'PY'
from pathlib import Path
import plistlib
import sys
file, base, data, support = map(Path, sys.argv[1:5])
pyrel = sys.argv[5]
document = {
    "Label": "com.aurapet.qwen-tts",
    "ProgramArguments": [
        str(base / pyrel), "-B", str(base / "server.py"),
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
