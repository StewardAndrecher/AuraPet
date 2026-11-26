# AuraPet 可选语音

轻量版包含系统语音、语音设置和音频播放器。在「回复语音…」里勾选「朗读桌宠发言」后，可用「使用系统发音」在 macOS 系统语音与千问之间切换。首次安装时两个开关默认为关闭。

## 选择下载

| 包 | 内容 | 用法 |
| --- | --- | --- |
| AuraPet 轻量版 | macOS 桌宠程序 | 解压并运行。常规聊天、气泡及文字记录可直接使用。 |
| Qwen 0.6B NVIDIA 模型包 | Qwen 官方 SafeTensors 权重、分词器与启动脚本 | 在安装 CUDA 版 PyTorch 的 Windows 或 Linux 机器上运行。 |
| Qwen 0.6B Apple Silicon 模型包 | 同一 0.6B Base 的 Q8_0 GGUF 权重、分词器与启动脚本 | 在 Apple Silicon 与 macOS 14 或以上运行 Metal 后端。 |

模型包可以解压到任意目录，与桌宠程序分开管理。模型包带权重，Python 依赖在首次配置时安装。

## 配置参考音频

1. 将 `voices.example.json` 复制为 `voices.json`。
2. 将 `ref_audio` 改成有权使用、无背景音乐的参考 WAV。相对路径以 `voices.json` 为基准。将 `ref_text` 填成这段音频实际说出的句子。
3. 中文默认音色名是 `zh`，日文是 `ja`。可保留其中一种。要区分多个角色，可以创建更多音色名，再在 AuraPet 里为各个角色选用对应名称。

导入 `.gifpet` 后，当前角色的分享包音色可从本机角色库读取。同一台 Mac 上使用默认的本机千问地址即可。独立运行语音服务时，可用 `--role-database` 指定角色库的 `roles.sqlite3`；服务以只读方式读取所选角色的参考 WAV 和转写。用户自定义的音色名仍可用于单独配置的音色。

## 运行语音服务

使用 Python 3.12，在解压的模型包目录创建独立环境。

```text
python -m venv .venv
```

Windows 使用 `.venv\Scripts\python.exe`，macOS 和 Linux 使用 `.venv/bin/python`。下方的 `python` 指独立环境内的解释器。

**NVIDIA**。按 [PyTorch 安装选择器](https://pytorch.org/get-started/locally/) 安装 CUDA 版 PyTorch，然后运行。

```text
python -m pip install -r requirements.txt
python server.py
```

**Apple Silicon**。运行以下命令使用模型包里的 GGUF。

```text
python -m pip install -r requirements.txt
python server.py --backend ggml
```

服务预热后会显示已就绪。打开桌宠的「回复语音…」，勾选「朗读桌宠发言」，取消勾选「使用系统发音」，保持默认地址 `http://127.0.0.1:17860/v1/audio/speech`，保存后发一条短消息。当服务和 AuraPet 位于不同电脑上时，可把远程服务的端口转发到运行 AuraPet 的电脑。服务默认只监听本机回环地址。

## 在角色卡里训练音色

右键桌宠打开「角色卡」，在「查看角色」切换本地角色。点击「编辑角色卡」填写身份与立场、AI 聊天角色设定及对话示例。保存后随角色包导出。下方按语言列出录音原文件名、台词和已关联的参考音色。

「导入语音」支持直接选择中文或日文 WAV 文件夹。可双击录音填写台词。批量导入录音和台词时，选择包含 `zh/train_raw.jsonl`、`ja/train_raw.jsonl` 或同路径 `validation_raw.jsonl` 的目录。JSONL 每条填写相对路径 `audio`、台词 `text`，可选原文件名 `name` 和参考音频 `ref_audio`。导入某个语种会替换该语种的语料清单。

点击「一键训练音色」会在 Apple Silicon 上使用 Qwen3-TTS 0.6B 的 MLX 版本分语种微调。需要 Python 3.11–3.13，建议 3.12。首次可能下载约 2 GB 的公开模型与训练依赖。录音、台词和权重在本机处理。训练使用每个语种所有已导入并有台词的录音，默认三轮。任务目录保留日志、按语种分开的 LoRA 权重，以及合成成功时的 `试听.wav`。

可在「打开训练目录」中查看产物，也可以关闭角色卡让任务继续在 AuraPet 中运行。退出 AuraPet 会终止任务。历史角色包仍可导入，其中缺少对应台词的旧录音需要补充台词。带台词的语料包需要 AuraPet 1.0.28 或更新版本。现有桌面发言继续使用已配置的参考音色，MLX 训练权重可单独试听。

## 双击随机发言

双击默认使用已接入的 AI 接口，参考当前角色的近期聊天和记忆生成发言。接口未配置或暂时失败时，会从勾选的本地项目中抽取。可在「角色卡 → 随机发言」关闭 AI 自动发言，或设置内置示例、自定义台词和原声。每个角色独立保存，本地发言在一轮内各条只出现一次。

角色原声在 Mac 上直接播放原 WAV，有转写时同步显示台词。原声随导出的完整语音语料分享。带 AI 自动发言开关的角色包要求 AuraPet 1.0.32 或更新版本。

## 播放行为

- 语音开始播放后，气泡文字随声音逐字出现，超出容量后正文自动上移。系统发音使用系统提供的朗读位置，千问发音和角色原声根据实际音频进度估算字幕位置。聊天记录保留完整原文。
- 本机千问首次合成会预热，收到第一段音频后即开播。等待超时、音频中断或语音服务退出时，本轮继续显示文字。开启「使用系统发音」后，下一轮直接从系统语音播放。
- AI 回复、双击后的自动发言和固定短句共用语音开关。打开「试听发音」可以验证声音。
- 合成语音前会自动略过中英文括号内的动作、常见颜文字和 Emoji。括号动作可嵌套，可以跨流式输出片段。气泡和聊天记录保留原文。
- 按 Esc 可结束当前朗读。发送新消息、切换角色、隐藏桌宠、关闭语音和开启免打扰也会清除旧音频。
- 音色参考和字幕在本地语音服务配置。桌宠向配置的语音地址发送当前需朗读的短句及音色名。

模型来自 [Qwen3-TTS-12Hz-0.6B-Base](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base)，许可为 Apache 2.0。流式运行依赖 [faster-qwen3-tts](https://github.com/andimarafioti/faster-qwen3-tts)，Apple Silicon 文件来自 [qwentts.cpp 的 GGUF 模型库](https://huggingface.co/Serveurperso/Qwen3-TTS-GGUF)。
