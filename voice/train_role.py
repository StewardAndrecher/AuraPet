#!/usr/bin/env python3
"""Train a Qwen3-TTS voice locally from an AuraPet role's WAV and transcript records."""
import argparse
from contextlib import closing
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import sqlite3
import subprocess
import sys
import time
import wave

ENGINE_URL = "https://github.com/akashicMarga/mlx-audio-train.git"
ENGINE_REV = "ffbb4898ee3c613346295cbb873fdb7c6f27be23"
MODEL = "mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit"
MODEL_REV = "50f45ef0047cde7e84c2ef04326acb8ada2436a7"
DEPS = (
    "mlx-audio==0.6.8", "mlx==0.32.4", "numpy==2.5.5", "scipy==1.18.1",
    "soundfile==0.14.0", "pyyaml==6.0.4", "safetensors==0.8.0",
    "huggingface-hub==2.4.0",
)
SKIP_LEVELS = ("[processor] Failed to process", "[processor] Warning", "[dataset] Failed to load",
               "[dataset] Error", "[dataset] Invalid", "[dataset] Skipped")
AUDIO_KEY = re.compile(r"voice/corpus/(zh|ja)/([0-9]{3})\.wav")
LANGUAGES = {"zh": "chinese", "ja": "japanese"}
PREVIEW_TEXT = {
    "zh": "博士，请先核对记录，再决定是否调整计划。",
    "ja": "ドクター、記録を確認してから、次の計画を決めましょう。",
}
_child = None


def interrupted(*_):
    if _child is not None and _child.poll() is None:
        try:
            os.killpg(_child.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    raise KeyboardInterrupt


def run(command, cwd=None, env=None, reject_skips=False):
    global _child
    with subprocess.Popen([str(arg) for arg in command], cwd=cwd, env=env, start_new_session=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace", bufsize=1) as process:
        _child = process
        skipped = False
        for line in process.stdout:
            print(line, end="", flush=True)
            skipped |= any(marker in line for marker in SKIP_LEVELS)
        code = process.wait()
        _child = None
        if code:
            raise RuntimeError(f"训练工具退出，错误码 {code}。")
        if reject_skips and skipped:
            raise RuntimeError("训练工具跳过了录音，需检查日志和语料。")


def checked_duration(blob):
    with wave.open(io.BytesIO(blob), "rb") as audio:
        frames = audio.getnframes()
        seconds = frames / audio.getframerate()
        if not 0.05 <= seconds <= 60:
            raise ValueError("每段录音需为 0.05 至 60 秒。请先分段或修复源录音。")
        if frames * audio.getnchannels() * audio.getsampwidth() != len(audio.readframes(frames)):
            raise ValueError("有 WAV 文件未完整导入。")
        return seconds


def prepare(database, role, dest):
    uri = database.resolve().as_uri() + "?mode=ro"
    jobs = {}
    with closing(sqlite3.connect(uri, uri=True)) as connection:
        connection.execute("BEGIN")
        entry = connection.execute("SELECT transcripts,voices FROM role_share WHERE role_id=?", (role,)).fetchone()
        if entry is None:
            raise ValueError("当前角色还没有导入语料。")
        transcripts = json.loads(entry[0])
        voices = json.loads(entry[1])
        records = connection.execute(
            "SELECT path,bytes FROM role_share_files WHERE role_id=? AND path LIKE 'voice/corpus/%' ORDER BY path",
            (role,),
        )
        missing = []
        for path, raw in records:
            match = AUDIO_KEY.fullmatch(path)
            if match is None:
                raise ValueError("发现无法辨认的语料路径。")
            code, ident = match.groups()
            text = transcripts.get(path, "")
            if not isinstance(text, str) or not text.strip():
                missing.append(path)
                continue
            seconds = checked_duration(raw)
            lang_root = dest / code
            sounds = lang_root / "wav"
            sounds.mkdir(parents=True, exist_ok=True)
            target = sounds / f"{ident}.wav"
            target.write_bytes(raw)
            jobs.setdefault(code, []).append({"audio": str(target), "text": text.strip(), "duration": seconds,
                                               "lang_code": LANGUAGES[code]})
        if missing:
            raise ValueError(f"还有 {len(missing)} 条录音缺少对应台词。请先补齐。")
        for code, rows in jobs.items():
            preferred = connection.execute(
                "SELECT bytes FROM role_share_files WHERE role_id=? AND path=?", (role, f"voice/ref-{code}.wav")
            ).fetchone()
            if preferred is not None:
                ref = dest / code / "reference.wav"
                checked_duration(preferred[0])
                ref.write_bytes(preferred[0])
                ref_text = voices.get(code, {}).get("ref_text", "")
            else:
                row = min(rows, key=lambda item: abs(item["duration"] - 5))
                ref = Path(row["audio"])
                ref_text = row["text"]
            (dest / code / "reference.json").write_text(
                json.dumps({"audio": str(ref), "text": ref_text}, ensure_ascii=False), encoding="utf-8",
            )
            for item in rows:
                item["ref_audio"] = str(ref)
            manifest = dest / code / "train.jsonl"
            manifest.write_text("".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rows), encoding="utf-8")
            print(f"{code} · 将训练 {len(rows)} 条录音，共 {sum(row['duration'] for row in rows):.1f} 秒。", flush=True)
    if not jobs:
        raise ValueError("当前角色没有训练语料。")
    return {code: dest / code / "train.jsonl" for code in jobs}


def bootstrap(cache):
    if sys.version_info < (3, 11) or sys.version_info >= (3, 14):
        raise ValueError("请使用 Python 3.11–3.13，例如 Homebrew 的 python3.12。")
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise ValueError("这个训练入口使用 Apple Silicon MLX。请使用原生 arm64 Python。")
    cache.mkdir(parents=True, exist_ok=True)
    source = cache / "mlx-audio-train"
    env = os.environ.copy()
    env.update(GIT_TERMINAL_PROMPT="0", HF_HUB_DISABLE_TELEMETRY="1", HF_HUB_DISABLE_IMPLICIT_TOKEN="1",
               HF_HOME=str(cache / "hf"), TOKENIZERS_PARALLELISM="false", PYTHONUNBUFFERED="1")
    git = shutil.which("git")
    if not git:
        raise ValueError("首次训练需要 Git，用于获取固定版本的训练工具。")
    if not (source / ".git").is_dir():
        if source.exists():
            raise ValueError("训练工具目录不完整，请先移走该目录再重试。")
        pending = cache / f".engine-{os.getpid()}"
        try:
            run([git, "init", pending], env=env)
            run([git, "-C", pending, "fetch", "--depth", "1", ENGINE_URL, ENGINE_REV], env=env)
            run([git, "-C", pending, "checkout", "--detach", "FETCH_HEAD"], env=env)
            pending.rename(source)
        finally:
            if pending.exists():
                shutil.rmtree(pending)
    actual = subprocess.check_output([git, "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != ENGINE_REV:
        raise ValueError("本地训练工具版本与 AuraPet 配置不匹配。")
    venv = cache / "venv"
    python = venv / "bin/python"
    stamp = cache / "deps.txt"
    fingerprint = "\n".join(DEPS)
    if not python.is_file() or not stamp.exists() or stamp.read_text() != fingerprint:
        uv = shutil.which("uv")
        found = Path.home() / ".local/bin/uv"
        uv = uv or (str(found) if found.is_file() else None)
        if uv:
            if not python.is_file():
                run([uv, "venv", "--python", sys.executable, venv], env=env)
            run([uv, "pip", "install", "--python", python, *DEPS], env=env)
        else:
            if not python.is_file():
                run([sys.executable, "-m", "venv", venv], env=env)
            run([python, "-m", "pip", "install", *DEPS], env=env)
        stamp.write_text(fingerprint)
    return source, python, env


def download_model(python, env, cache):
    local = cache / "local-model"
    if (local / "config.json").is_file() and (local / "speech_tokenizer/model.safetensors").is_file():
        print("使用本机已有的 Qwen 0.6B MLX 权重。", flush=True)
        return local
    marker = cache / "model-location.txt"
    command = ("from pathlib import Path;from huggingface_hub import snapshot_download;"
               "path=snapshot_download(" + repr(MODEL) + ", revision=" + repr(MODEL_REV)
               + ", allow_patterns=['*.safetensors','*.json','*.txt','*.model']);"
               + "Path(" + repr(str(marker)) + ").write_text(path)")
    print("正在检查或下载 Qwen 0.6B MLX 权重（约 2 GB）。", flush=True)
    run([python, "-c", command], env=env)
    path = Path(marker.read_text())
    if not (path / "config.json").is_file() or not (path / "speech_tokenizer/model.safetensors").is_file():
        raise RuntimeError("模型下载不完整。")
    return path


def preview(checkout, python, env, model, language, directory):
    command = """
import sys
import json
from pathlib import Path
import numpy as np
import soundfile as sf
from mlx_audio.tts.utils import load_model
sys.path.insert(0, sys.argv[1])
from train.lora import LoRAConfig, apply_lora, load_adapters
root = Path(sys.argv[3])
config = json.loads((root / "training.json").read_text())
reference = json.loads((root / "reference.json").read_text())
lora = config["lora"]
tts = load_model(sys.argv[2])
apply_lora(tts, LoRAConfig(rank=lora["rank"], alpha=lora["alpha"], dropout=0,
                           target_modules=lora["target_modules"], model_type="qwen3_tts"))
load_adapters(tts, str(root / "checkpoints/checkpoint-final/adapters.safetensors"))
tts.eval()
parts = list(tts.generate(text=sys.argv[5], lang_code=sys.argv[4],
                          ref_audio=reference["audio"], ref_text=reference["text"] or None,
                          max_tokens=220))
audio = np.concatenate([np.asarray(part.audio, dtype=np.float32).reshape(-1) for part in parts])
if audio.size < 2400 or not np.all(np.isfinite(audio)) or np.max(np.abs(audio)) < .0001:
    raise RuntimeError("训练结果没有生成可试听的音频。")
sf.write(root / "试听.wav", audio, int(parts[0].sample_rate))
"""
    run([python, "-c", command, checkout, model, directory, LANGUAGES[language], PREVIEW_TEXT[language]], env=env)


def train(source, python, env, jobs, model, epochs, max_steps):
    for code, manifest in jobs.items():
        root = manifest.parent
        checkpoint = root / "checkpoints"
        cfg = root / "training.json"
        records = [json.loads(line) for line in manifest.read_text().splitlines()]
        accumulation = math.gcd(len(records), 4)
        config = {
            "model": {"model_id": str(model), "tokenizer_id": str(model), "model_type": "qwen3_tts_speaker"},
            "lora": {"rank": 8, "alpha": 16.0, "dropout": 0.0,
                     "target_modules": ["q_proj", "k_proj", "v_proj", "o_proj"]},
            "data": {"train_jsonl": str(manifest), "target_sr": 24000, "min_duration": .05, "max_duration": 60,
                     "trim": False, "normalize": False},
            "processor": {"max_text_len": 512, "max_codec_len": 1024,
                          "speaker_name": f"aurapet_{code}", "include_ref_mel": True},
            "trainer": {"output_dir": str(checkpoint), "run_name": f"aurapet_{code}",
                        "num_epochs": epochs, "max_steps": max_steps, "batch_size": 1, "grad_accumulation": accumulation,
                        "learning_rate": 2e-5, "grad_clip": 1.0, "warmup_steps": 0, "lr_schedule": "constant",
                        "save_every_n_epochs": 1, "save_every_n_steps": 100, "keep_last_n": 2,
                        "log_every_n_steps": 1, "prefetch": 0, "lang_code": LANGUAGES[code]},
        }
        # The pinned engine reads YAML 1.1, which interprets JSON's 2e-05 as a string.
        cfg.write_text(json.dumps(config, ensure_ascii=False, indent=2).replace("2e-05", "0.00002"), encoding="utf-8")
        print(f"{code} · 开始 Qwen LoRA 微调。", flush=True)
        run([python, source / "scripts/train.py", "--config", cfg], cwd=source, env=env, reject_skips=True)
        adapter = checkpoint / "checkpoint-final" / "adapters.safetensors"
        if not adapter.is_file() or not adapter.stat().st_size:
            raise RuntimeError("训练没有生成可用的 LoRA 权重。")
        print(f"{code} · 音色权重已保存到 {adapter}", flush=True)
        if max_steps is None:
            try:
                preview(source, python, env, model, code, root)
                print(f"{code} · 已生成试听.wav。", flush=True)
            except (RuntimeError, OSError, subprocess.SubprocessError) as exc:
                print(f"{code} · 权重已保存，试听生成失败：{exc}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--role", required=True)
    parser.add_argument("--job", type=Path, required=True)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--model-path", type=Path, help="Use an existing local MLX checkpoint")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--epochs", type=int, default=3, choices=range(1, 6))
    parser.add_argument("--max-steps", type=int, help="For local smoke tests only")
    args = parser.parse_args()
    dest = args.job.resolve()
    dest.mkdir(parents=True, exist_ok=False)
    jobs = prepare(args.database, args.role, dest)
    if args.prepare_only:
        return
    cache = (args.cache or dest.parent / "runtime").resolve()
    source, python, env = bootstrap(cache)
    model = args.model_path.resolve() if args.model_path else download_model(python, env, cache)
    train(source, python, env, jobs, model, args.epochs, args.max_steps)
    (dest / "completed.json").write_text(json.dumps({"languages": list(jobs), "finish": int(time.time()),
        "smoke_test": args.max_steps is not None}), encoding="utf-8")


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError, RuntimeError, sqlite3.Error, subprocess.SubprocessError, wave.Error) as exc:
        print(f"训练未完成：{exc}", file=sys.stderr, flush=True)
        sys.exit(1)
