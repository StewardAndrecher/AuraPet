#!/usr/bin/env python3
"""Optional local Qwen PCM streaming service for AuraPet."""

import argparse
from collections import OrderedDict
from contextlib import closing
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
from tempfile import TemporaryDirectory
from threading import Lock


ROOT = Path(__file__).resolve().parent
MODEL_ID = "Qwen/Qwen3-TTS-12Hz-0.6B-Base"
SAMPLE_RATE = 24000
ROLE_VOICE = re.compile(r"gifpet-role-([a-z0-9-]{1,40})-(zh|ja)")
LANGUAGES = {"zh": "Chinese", "ja": "Japanese"}
PROBE_TEXT = {"Chinese": "你好，我准备好了。", "Japanese": "こんにちは。準備できました。"}


def load_voices(path):
    if not path.exists():
        return {}
    raw = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(raw, dict):
        raise ValueError("voices.json 必须是音色配置对象。")
    voices = {}
    for name, entry in raw.items():
        if not isinstance(entry, dict):
            raise ValueError(f"音色 {name} 需要配置 ref_audio、ref_text、language。")
        audio = (path.parent / entry["ref_audio"]).resolve()
        transcript = entry["ref_text"].strip()
        if not audio.is_file() or not transcript:
            raise ValueError(f"音色 {name} 需要可读的参考录音及准确台词。")
        voices[name] = {
            "ref_audio": str(audio),
            "ref_text": transcript,
            "language": entry.get("language", "Chinese"),
        }
    return voices


def role_reference(database, name):
    match = ROLE_VOICE.fullmatch(name)
    if match is None or not database.is_file():
        return None
    role_id, language = match.groups()
    path = f"voice/ref-{language}.wav"
    try:
        with closing(sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True, timeout=1)) as connection:
            row = connection.execute(
                "SELECT s.voices, f.bytes FROM role_share s "
                "JOIN role_share_files f ON f.role_id=s.role_id "
                "WHERE s.role_id=? AND f.path=? AND length(f.bytes) BETWEEN 45 AND 16777216",
                (role_id, path),
            ).fetchone()
        if row is not None:
            profile = json.loads(row[0])[language]
            transcript = profile["ref_text"]
            if profile["ref_audio"] == path and isinstance(transcript, str) and 0 < len(transcript.strip()) <= 800:
                return row[1], transcript.strip(), LANGUAGES[language]
    except (sqlite3.Error, ValueError, KeyError, TypeError):
        return None
    return None


class QwenSpeech:
    def __init__(self, args, voices):
        import numpy as np
        self.np = np
        self.voices = voices
        self.backend = args.backend
        self.role_database = args.role_database
        self.role_samples = TemporaryDirectory(prefix="gif-pet-audio-")
        self.role_cache = OrderedDict()
        self.lock = Lock()
        self.prompts = {}
        if self.backend in {"ggml", "metal"} and (not args.talker.is_file() or not args.codec.is_file()):
            raise ValueError("请解压 Apple Silicon 模型包，或通过 --talker 和 --codec 指定 GGUF。")
        if self.backend == "metal":
            from metal_backend import MetalQwen
            self.model = MetalQwen(args.talker, args.codec)
        elif self.backend == "ggml":
            from faster_qwen3_tts import FasterQwen3TTS
            self.model = FasterQwen3TTS.from_pretrained(
                MODEL_ID, backend="ggml", gguf_talker_path=str(args.talker.resolve()),
                gguf_codec_path=str(args.codec.resolve()),
            )
        else:
            import torch
            from faster_qwen3_tts import FasterQwen3TTS

            if not torch.cuda.is_available():
                raise ValueError("CUDA 不可用。请安装支持 NVIDIA CUDA 的 PyTorch，或在 Apple Silicon 使用 GGUF。")
            if not (args.model / "model.safetensors").is_file():
                raise ValueError("请解压官方 Qwen 模型包，或用 --model 指定模型目录。")
            self.model = FasterQwen3TTS.from_pretrained(
                str(args.model.resolve()), backend="torch", dtype=torch.bfloat16, device="cuda:0",
            )
            for name, voice in voices.items():
                self.prompts[name] = self.model.model.create_voice_clone_prompt(
                    ref_audio=voice["ref_audio"], ref_text=voice["ref_text"],
                )

    def packaged_voice(self, reference):
        audio, transcript, language = reference
        digest = sha256(audio)
        digest.update(transcript.encode("utf-8"))
        digest.update(language.encode("ascii"))
        key = digest.hexdigest()
        if key in self.role_cache:
            self.role_cache.move_to_end(key)
            return self.role_cache[key][1:]
        path = Path(self.role_samples.name) / (key + ".wav")
        path.write_bytes(audio)
        voice = {"ref_audio": str(path), "ref_text": transcript, "language": language}
        try:
            options = {}
            if self.backend == "torch":
                options = {
                    "non_streaming_mode": False,
                    "voice_clone_prompt": self.model.model.create_voice_clone_prompt(
                        ref_audio=voice["ref_audio"], ref_text=transcript,
                    ),
                }
        except Exception:
            path.unlink(missing_ok=True)
            raise
        self.role_cache[key] = (path, voice, options)
        if len(self.role_cache) > 8:
            _, (old_path, _, _) = self.role_cache.popitem(last=False)
            old_path.unlink(missing_ok=True)
        return voice, options

    def stream(self, name, text, reference=None):
        with self.lock:
            if reference is None:
                voice = self.voices[name]
                options = {"non_streaming_mode": False, "voice_clone_prompt": self.prompts[name]} if name in self.prompts else {}
            else:
                voice, options = self.packaged_voice(reference)
            with closing(self.model.generate_voice_clone_streaming(
                text=text, chunk_size=8, **voice, **options,
            )) as generated:
                for audio, rate, _ in generated:
                    if rate != SAMPLE_RATE:
                        raise ValueError(f"期望 24000Hz PCM，服务获得了 {rate}Hz。")
                    pcm = self.np.clip(audio * 32768, -32768, 32767).astype("<i2").tobytes()
                    if pcm:
                        yield pcm

    def warm_up(self):
        if self.backend == "metal" and not self.voices:
            self.model.warm_up()
        for name, voice in self.voices.items():
            chunks = self.stream(name, PROBE_TEXT.get(voice["language"], "Hello. I am ready."))
            with closing(chunks):
                for _ in chunks:
                    pass


def make_handler(engine):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            if self.path != "/health":
                self.send_error(404)
                return
            self.reply({"ready": True, "sample_rate": SAMPLE_RATE, "voices": list(engine.voices)})

        def reply(self, value):
            body = json.dumps(value).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            if self.path != "/v1/audio/speech":
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 16_384:
                    raise ValueError
                data = json.loads(self.rfile.read(length))
                text, voice = data["input"], data["voice"]
                if (not isinstance(text, str) or not 0 < len(text.strip()) <= 350
                    or not isinstance(voice, str) or not 0 < len(voice) <= 80
                    or data.get("response_format", "pcm") != "pcm"):
                    raise ValueError
            except (ValueError, KeyError, TypeError):
                self.send_error(400, "Invalid text, voice or audio format")
                self.close_connection = True
                return
            reference = None
            if voice not in engine.voices:
                reference = role_reference(engine.role_database, voice)
                if reference is None:
                    self.send_error(400, "Voice not found")
                    return
            chunks = engine.stream(voice, text, reference)
            with closing(chunks):
                try:
                    first = next(chunks)
                except Exception as error:
                    self.log_error("synthesis failed: %s", error)
                    self.send_error(503, "Audio unavailable")
                    return
                try:
                    self.send_response(200)
                    self.send_header("Content-Type", "audio/pcm")
                    self.send_header("Transfer-Encoding", "chunked")
                    self.end_headers()
                    self.write_chunk(first)
                    for data in chunks:
                        self.write_chunk(data)
                    self.wfile.write(b"0\r\n\r\n")
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, TimeoutError):
                    self.close_connection = True
                except Exception as error:
                    self.log_error("audio stream stopped: %s", error)
                    self.close_connection = True

        def write_chunk(self, data):
            self.wfile.write(f"{len(data):x}\r\n".encode("ascii"))
            self.wfile.write(data)
            self.wfile.write(b"\r\n")
            self.wfile.flush()

    return Handler


def arguments():
    parser = argparse.ArgumentParser(description="AuraPet Qwen3-TTS 0.6B 本机流式语音服务")
    parser.add_argument("--backend", choices=("torch", "ggml", "metal"), default="torch")
    parser.add_argument("--port", type=int, default=17860)
    database = ROOT.parent / "roles.sqlite3"
    if sys.platform == "darwin" and not database.exists():
        database = Path.home() / "Library" / "Application Support" / "Gif-Pet" / "Gif-Pet" / "roles.sqlite3"
    parser.add_argument("--role-database", type=Path, default=database)
    parser.add_argument("--voices", type=Path, default=ROOT / "voices.json")
    parser.add_argument("--model", type=Path, default=ROOT / "Qwen3-TTS-12Hz-0.6B-Base")
    parser.add_argument("--talker", type=Path, default=ROOT / "gguf" / "qwen-talker-0.6b-base-Q8_0.gguf")
    parser.add_argument("--codec", type=Path, default=ROOT / "gguf" / "qwen-tokenizer-12hz-Q8_0.gguf")
    return parser.parse_args()


def main():
    args = arguments()
    if args.backend in {"ggml", "metal"} and sys.platform == "darwin":
        os.environ.setdefault("GGML_BACKEND", "MTL0")
    try:
        voices = load_voices(args.voices)
        engine = QwenSpeech(args, voices)
        print("正在预热音色…", flush=True)
        engine.warm_up()
    except Exception as error:
        raise SystemExit(f"语音服务无法启动 · {error}") from error
    with ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(engine)) as server:
        print(f"语音服务就绪 http://127.0.0.1:{args.port}/v1/audio/speech", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
