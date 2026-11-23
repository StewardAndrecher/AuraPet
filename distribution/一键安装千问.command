#!/bin/bash
set -euo pipefail
bundle="$(cd "$(dirname "$0")" && pwd -P)"
exec /bin/bash "$bundle/安装 AuraPet.command" --voice-only "$@"
