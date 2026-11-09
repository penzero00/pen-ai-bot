"""Generate clean (not over-processed) neural Hello for MAX98357A.
Loudness comes from module GAIN->GND (12dB); keep digital headroom."""
from __future__ import annotations

import asyncio
import pathlib
import subprocess
import wave

import edge_tts
import imageio_ffmpeg
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
MP3_PATH = ROOT / "firmware" / "src" / "hello_raw.mp3"
WAV_PATH = ROOT / "firmware" / "src" / "hello_raw.wav"
HDR_PATH = ROOT / "firmware" / "include" / "hello_pcm.h"

VOICE = "en-US-AriaNeural"
TEXT = "Hello!"
TARGET_RATE = 44100
# Leave headroom — GAIN@GND already adds +12dB. Hot PCM = static on Class-D.
TARGET_PEAK = 0.55


async def synthesize_mp3(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    communicate = edge_tts.Communicate(TEXT, VOICE, rate="-10%", volume="+50%", pitch="+0Hz")
    await communicate.save(str(path))


def mp3_to_wav(mp3: pathlib.Path, wav: pathlib.Path) -> None:
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    # Mild leveling only — no aggressive dynaudnorm (that causes pumping/static)
    af = f"aformat=sample_fmts=s16:channel_layouts=mono,aresample={TARGET_RATE},loudnorm=I=-16:TP=-3:LRA=11"
    cmd = [
        ffmpeg, "-y", "-i", str(mp3),
        "-vn", "-af", af,
        "-ac", "1", "-ar", str(TARGET_RATE), "-sample_fmt", "s16",
        str(wav),
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def read_wav_mono(path: pathlib.Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as w:
        nch, sw, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())
    if sw != 2:
        raise RuntimeError(f"expected 16-bit, got {sw}")
    data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if nch > 1:
        data = data.reshape(-1, nch).mean(axis=1)
    return data, rate


def one_pole_lpf(x: np.ndarray, cutoff_hz: float, rate: int) -> np.ndarray:
    rc = 1.0 / (2.0 * np.pi * cutoff_hz)
    dt = 1.0 / rate
    a = dt / (rc + dt)
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += a * (v - acc)
        y[i] = acc
    return y


def one_pole_hpf(x: np.ndarray, cutoff_hz: float, rate: int) -> np.ndarray:
    rc = 1.0 / (2.0 * np.pi * cutoff_hz)
    dt = 1.0 / rate
    a = rc / (rc + dt)
    y = np.empty_like(x)
    prev_x = prev_y = 0.0
    for i, v in enumerate(x):
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        y[i] = prev_y
    return y


def fade_edges(x: np.ndarray, rate: int, ms: float = 12.0) -> np.ndarray:
    n = int(rate * ms / 1000.0)
    if n <= 1 or len(x) < n * 2:
        return x
    y = x.copy()
    ramp = np.linspace(0.0, 1.0, n)
    y[:n] *= ramp
    y[-n:] *= ramp[::-1]
    return y


def process(mono: np.ndarray, src_rate: int) -> np.ndarray:
    thresh = 0.008
    nz = np.where(np.abs(mono) > thresh)[0]
    if len(nz) == 0:
        raise RuntimeError("silence")
    pad = int(src_rate * 0.03)
    mono = mono[max(0, nz[0] - pad) : min(len(mono), nz[-1] + pad)]

    if src_rate != TARGET_RATE:
        n_new = max(1, int(round(len(mono) * TARGET_RATE / src_rate)))
        t_old = np.linspace(0.0, 1.0, num=len(mono), endpoint=False)
        t_new = np.linspace(0.0, 1.0, num=n_new, endpoint=False)
        mono = np.interp(t_new, t_old, mono)

    # Gentle cleanup only — no heavy presence boost / hard compression
    mono = one_pole_hpf(mono, 90.0, TARGET_RATE)
    mono = one_pole_lpf(mono, 5500.0, TARGET_RATE)  # kill brittle Class-D hiss
    mono = fade_edges(mono, TARGET_RATE, 12.0)

    peak = float(np.max(np.abs(mono)) or 1.0)
    mono = mono / peak * TARGET_PEAK
    return np.clip(mono * 32767.0, -32767, 32767).astype(np.int16)


def write_header(samples: np.ndarray) -> None:
    rms = float(np.sqrt(np.mean((samples.astype(np.float64) / 32768.0) ** 2)))
    with HDR_PATH.open("w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("// Clean neural Hello — headroom left for GAIN@GND (+12dB)\n")
        f.write(f"// Voice={VOICE} peak={TARGET_PEAK} rms={rms:.3f}\n")
        f.write(f"#define HELLO_SAMPLE_RATE {TARGET_RATE}\n")
        f.write(f"#define HELLO_SAMPLE_COUNT {len(samples)}\n")
        f.write("static const int16_t HELLO_PCM[HELLO_SAMPLE_COUNT] = {\n")
        for i in range(0, len(samples), 12):
            chunk = ", ".join(str(int(v)) for v in samples[i : i + 12])
            f.write(f"  {chunk},\n")
        f.write("};\n")
    print(f"peak={int(np.max(np.abs(samples)))} rms={rms:.3f} dur={len(samples)/TARGET_RATE:.2f}s")


async def main() -> None:
    print(f"Clean Hello: {VOICE}")
    await synthesize_mp3(MP3_PATH)
    mp3_to_wav(MP3_PATH, WAV_PATH)
    mono, rate = read_wav_mono(WAV_PATH)
    pcm = process(mono, rate)
    write_header(pcm)
    MP3_PATH.unlink(missing_ok=True)
    WAV_PATH.unlink(missing_ok=True)
    print(f"wrote {HDR_PATH}")


if __name__ == "__main__":
    asyncio.run(main())
