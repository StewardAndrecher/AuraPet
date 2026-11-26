# AuraPet

跨平台开源轻量级 AI 桌面伙伴（支持 **macOS 14+ Apple Silicon** 与 **Windows 10/11 x64**）。软件本体**不内置任何演示角色**，保持极简干净，完全由玩家按需导入透明 GIF / WebP / PNG / AVIF 素材或 `.gifpet` 角色包；支持多宠同屏、智能陪伴模式、专注番茄钟、OpenAI 兼容流式 AI 对话与动作旁白自然停顿、长期记忆，以及可选本机千问（Qwen3-TTS）流式配音。

<p align="center">
  <img src="docs/demo/kaltsit-demo.gif" alt="AuraPet 凯尔希思衡托导入与运行演示" width="220" />
</p>
<p align="center"><sub>演示素材「凯尔希思衡托」已获原作者授权，仅用于 GitHub 演示与独立可选角色包分发，不包含在 AuraPet 软件本体内。</sub></p>

## 文档导航

- **[安装与部署文档（docs/安装文档.md）](docs/安装文档.md)**：三类发行包选择、macOS / Windows 懒人一键部署、凯尔希角色包导入、开发者编译与问题排查。
- **[功能使用文档（docs/使用文档.md）](docs/使用文档.md)**：交互手势、多宠角色库、智能陪伴模式、专注番茄钟、AI 对话与旁白停顿、千问语音与隐私安全。

## 下载与懒人一键部署

前往 [GitHub Releases](https://github.com/StewardAndrecher/AuraPet/releases/latest) 按需下载：

| 发行文件 | 说明 | 大小（约） |
| --- | --- | --- |
| **`AuraPet-<版本>-macOS-arm64-Light.zip`** | **无千问极简安装包（macOS）**：含桌面主程序、系统发音、角色原声与一键安装脚本 | ~32 MB |
| **`AuraPet-<版本>-macOS-arm64-Qwen-Offline.zip`** | **带千问全量包（macOS）**：内置已校验 Qwen3-TTS 0.6B GGUF 权重与千问一键部署脚本 | ~1.25 GB |
| **`AuraPet-<版本>-Windows-x64-Light.zip`** | **无千问极简安装包（Windows）**：含桌面主程序与免管理员一键安装脚本 | ~35 MB |
| **`AuraPet-<版本>-Kaltsit-RolePack.gifpet`** | **凯尔希·思衡托角色包（可选下载）**：含动图、角色卡人设与中日参考音色，玩家自由选择下载导入 | ~50 MB |

### macOS 一键部署（Apple Silicon，macOS 14.0+）
1. 下载并解压 **无千问极简安装包** 或 **带千问全量包**。
2. 双击目录中的 **`安装 AuraPet.command`**，按弹窗选择「一键安装 AuraPet」或「同时安装千问」。
3. 脚本会自动解除系统隔离属性、校验完整性、安装到 `~/Applications/AuraPet.app` 并启动。

### Windows 一键部署（Windows 10 / 11 x64）
1. 下载并解压 `AuraPet-<版本>-Windows-x64-Light.zip`。
2. 双击 **`安装 AuraPet.bat`**，按提示选择仅装桌宠或同时配置千问语音，自动完成免管理员安装、快捷方式创建与 `.gifpet` 关联。

### 导入角色（支持凯尔希角色包或任意自备素材）
首次启动 AuraPet 会弹出导入引导：
- 若下载了可选的 **`AuraPet-<版本>-Kaltsit-RolePack.gifpet`**，直接**双击**或**拖入窗口**即可完成凯尔希·思衡托一键导入。
- 也可随时拖入透明 GIF、WebP、PNG、JPEG 或静态 AVIF（macOS 支持本机 Apple Vision 智能抠图）。

## 核心特性

- **极简本体 & 多宠同屏**：不捆绑任何演示素材；角色库支持多桌宠同屏，每个角色独立保存位置、缩放、陪伴模式、活力值、动作槽与对话记忆。
- **智能陪伴模式与物理漫步**：支持 **自动 / 游玩 / 日常 / 静伴** 四档切换，「自动」模式智能感知前台活跃窗口与外部全屏状态动态调节移动与发言频率；支持「逗一逗」「充个电」互动与右键长按「灵犀牵引」。
- **专注计时与可定制表现**：内置 15 / 25 / 50 分钟及自定义专注倒计时与全局快捷键（macOS `⌃⌥A/F/D`，Windows `Ctrl+Alt+A/F/D`），偏好设置中可自由切换专注时「静伴微动」或「完全静止」。
- **AI 对话、旁白停顿与长期记忆**：单击桌宠呼出浮层流式对话，兼容任意 OpenAI 接口；自动识别括号动作描写 `（...）` 不发声并按阅读节奏插入自然停顿，保证气泡观赏与语音连贯；支持长期记忆整理与遗忘。
- **流式语音朗读与角色原声**：支持系统语音、本机 Qwen3-TTS 流式朗读、角色原声 WAV 播放，以及 macOS Apple Silicon 本地 MLX 一键音色微调。
- **macOS 原生增强**：Apple Vision 前景抠图、跨虚拟桌面（Spaces）方向感知跃入与气泡随行续写、「活跃窗口下第二层」置序。

## 隐私与 API 密钥安全

API Key 仅保存在本机账户私有目录中，**绝不会**进入源码、安装包或 `.gifpet` 角色包：
- **macOS**：`~/Library/Application Support/AuraPet/chat-credentials/<接口地址 SHA-256>`（`0700`/`0600` 权限）。
- **Windows**：`%LOCALAPPDATA%\AuraPet\chat-credentials\<接口地址 SHA-256>`（Windows DPAPI 绑定账户加密）。
- **安全分享**：导出 `.gifpet` 采用严格白名单，绝不包含 API Key、接口地址或私人聊天记录。

## 开源许可与第三方组件

本项目源码基于 [MIT License](LICENSE) 开源。二进制发行包附带的 Qt 6（LGPL-3.0）、libavif、libarchive 以及可选 Qwen3-TTS 组件的许可说明详见 [distribution/第三方组件.md](distribution/第三方组件.md)。开发者编译与 CI 发版流程见 [docs/安装文档.md](docs/安装文档.md#6-开发者从源码编译与本地打包)。
