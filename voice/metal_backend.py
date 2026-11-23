"""Small Mac runtime for the packaged Metal models and the streaming service."""

from collections import OrderedDict
from contextlib import closing
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import wave

import numpy as np
from qwentts_cpp import QwenTTS

SAMPLE_RATE = 24_000


class MetalQwen:
    def __init__(self, talker, codec):
        self.runtime = QwenTTS(talker, codec, log_level="warning")
        self.references = OrderedDict()

    def warm_up(self):
        # A short local tone initializes the native kernels without shipping a voice recording.
        time = np.arange(SAMPLE_RATE * 2, dtype=np.float32) / SAMPLE_RATE
        tone = (np.sin(2 * np.pi * 165 * time) * np.sin(np.pi * time / 2) ** 2).astype(np.float32) * .12
        reference = self.runtime.extract_voice_ref(tone)
        packets = self.runtime.stream(
            text="你好。", lang="chinese", ref_text="你好。", ref_spk_emb=reference.ref_spk_emb,
            ref_codes=reference.ref_codes, first_chunk_frames=2, max_new_tokens=10,
        )
        with closing(packets):
            for _ in packets:
                pass

    def reference(self, filename):
        source = Path(filename)
        info = source.stat()
        key = (str(source), info.st_size, info.st_mtime_ns)
        if key in self.references:
            self.references.move_to_end(key)
            return self.references[key]
        with TemporaryDirectory(prefix="aurapet-voice-") as folder:
            converted = Path(folder) / "mono.wav"
            subprocess.run(
                ["/usr/bin/afconvert", "-f", "WAVE", "-d", "LEI16@24000", "-c", "1", str(source), str(converted)],
                check=True, timeout=20, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            with wave.open(str(converted), "rb") as audio:
                length = audio.getnframes()
                if length < SAMPLE_RATE or length > 30 * SAMPLE_RATE:
                    raise ValueError("参考录音需要是 1 到 30 秒的清晰人声。")
                pcm = np.frombuffer(audio.readframes(length), dtype="<i2").astype(np.float32) / 32768
        pcm = np.pad(pcm, (0, SAMPLE_RATE // 5))
        reference = self.runtime.extract_voice_ref(pcm)
        self.references[key] = reference
        if len(self.references) > 8:
            self.references.popitem(last=False)
        return reference

    def generate_voice_clone_streaming(self, *, text, chunk_size, ref_audio, ref_text, language):
        reference = self.reference(ref_audio)
        samples = self.runtime.stream(
            text=text, lang=language.lower(), ref_spk_emb=reference.ref_spk_emb,
            ref_codes=reference.ref_codes, ref_text=ref_text,
            first_chunk_frames=4, codec_chunk_sec=chunk_size / 12.5,
        )
        with closing(samples):
            for waveform, rate in samples:
                yield waveform, rate, {}
