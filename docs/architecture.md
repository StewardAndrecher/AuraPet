# AuraPet 架构

AuraPet 是基于 Qt 6 Widgets 的 macOS 桌宠。C++ 应用负责桌面交互和本地数据，Objective-C++ 调用 macOS 图像、钥匙串和音频接口。可选的 Qwen 服务独立运行，应用通过本机 HTTP 请求流式 PCM。

| 入口 | 负责内容 |
| --- | --- |
| `main.cpp` | 应用启动、对象组装、菜单和角色切换 |
| `pet_window.*`、`random_movement.*`、`pet_motion_rules.*` | 桌宠窗口、跨屏定位、自动移动和活跃度规则 |
| `role_library.*`、`role_share.cpp`、`role_package.*` | SQLite 角色库、导入事务和 `.gifpet` 发布格式 |
| `chat_session.*`、`role_memory.cpp` | 聊天请求、历史、摘要和长期记忆 |
| `focus_session.*`、`companion_events.*`、`global_hotkeys.*` | 计时、事件调度和全局快捷键 |
| `speech_playback.*`、`speech_audio.mm`、`voice/` | 语音分段、系统发音与可选本机千问服务 |
| `distribution/`、`scripts/install.sh` | 原创默认素材、打包脚本与用户安装入口 |

命令行安装脚本在校验归档 SHA-256 后调用包内安装器。轻量包与离线包共享应用和安装器，模型及运行时单独管理。历史包使用的 `.gifpet` 扩展名和本地数据路径继续兼容。
