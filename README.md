# AuraPet

<img align="right" src="docs/assets/glow.gif" width="150" alt="原创桌宠光团上下浮动">

在 Mac 桌面上养一只会活动、陪你专注、也能聊天的小伙伴。把自己的图片或 GIF 拖进来，即可制作角色并分享给别人。

**Apple Silicon · macOS 26.6+ · MIT**

[项目展示](https://stewardandrecher.github.io/AuraPet/) · [下载预览版](https://github.com/StewardAndrecher/AuraPet/releases/tag/v1.0.27) · [使用手册](docs/guide.md)

## 安装

从 [v1.0.27 下载页](https://github.com/StewardAndrecher/AuraPet/releases/tag/v1.0.27) 选择轻量包（41 MiB）或千问离线包（约 1.2 GiB）。解压后双击「安装 AuraPet.command」，选择「仅安装 AuraPet」或「同时安装千问」。程序安装到 `~/Applications/AuraPet.app`，无需管理员权限。

**终端一键安装**（默认只装桌宠）

```bash
p="$(mktemp -t aurapet-install)" && curl -fLsS --proto '=https' --proto-redir '=https' https://raw.githubusercontent.com/StewardAndrecher/AuraPet/v1.0.27/scripts/install.sh -o "$p" && /bin/bash "$p" --light
```

末尾用 `--with-qwen` 可同时安装千问，或用 `--offline-package` 下载含权重的完整包。脚本会校验安装包的 SHA-256。桌宠和系统语音可单独使用。AI 聊天需填入你自己的 OpenAI 兼容接口，千问音色需导入有权使用的参考录音。

> 安装包是 macOS 技术预览，使用临时签名。公开下载时系统可能提示无法验证开发者。你也可以按下面的步骤从源码构建。

## 能做什么

- **玩素材** · 播放透明 GIF、PNG、JPEG 和静态 AVIF，支持本机抠图、拖拽、置顶、多屏和自定义动作。
- **做角色** · 编辑角色卡、动作槽、音色、猫粮和日常触发器，使用一个 `.gifpet` 文件分享。
- **陪你工作** · 专注与休息计时、免打扰、早晚问候、今日统计，以及显示和隐藏桌宠的全局快捷键。
- **和角色聊天** · 流式对话、本地 SQLite 历史和可编辑记忆，可选系统朗读或本机千问语音。

聊天和记忆整理会向你配置的服务发送所需的对话上下文。千问默认只在本机运行，导出角色包时个人聊天和接口密钥仍保存在本机。详见[使用手册](docs/guide.md)和[语音配置](voice/README.md)。

## 从源码构建

```bash
brew install cmake qtbase libavif
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=26.6 \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase);$(brew --prefix libavif)"
cmake --build build --parallel
open build/AuraPet.app
```

需要 Xcode 命令行工具。见[架构与文件入口](docs/architecture.md)和[第三方许可](distribution/第三方组件.md)。项目源码采用 [MIT 许可证](LICENSE)。Qt、千问权重与其他依赖沿用各自许可证。
