"""Stream laptop audio to ESP32 MAX98357A using framed PCM (alignment-safe)."""
from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess
import sys
import time

import imageio_ffmpeg
import serial
from serial.tools import list_ports

STREAM_RATE = 16000
SERIAL_BAUD = 921600
PKT_SAMPLES = 256  # must be <= firmware MAX_PKT (512)


def find_port(preferred: str | None) -> str:
    if preferred:
        return preferred
    ports = list(list_ports.comports())
    for p in ports:
        h = f"{p.description} {p.hwid}".lower()
        if "1a86" in h or "ch340" in h or "ch343" in h or "serial" in h:
            return p.device
    if not ports:
        raise SystemExit("No serial port found.")
    return ports[0].device


def open_pcm_loop(path: pathlib.Path):
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    # Decode + gentle loudness + speech/music friendly filter for small speaker
    # dynaudnorm keeps volume high without hard clip hash
    af = f"aformat=sample_fmts=s16:channel_layouts=mono,aresample={STREAM_RATE},dynaudnorm=f=75:g=15,volume=1.4"
    cmd = [
        ffmpeg, "-hide_banner", "-loglevel", "error",
        "-stream_loop", "-1",
        "-i", str(path),
        "-vn",
        "-af", af,
        "-f", "s16le",
        "-acodec", "pcm_s16le",
        "pipe:1",
    ]
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def send_packet(ser: serial.Serial, samples: bytes) -> None:
    n = len(samples) // 2
    if n <= 0:
        return
    header = struct.pack("<BBH", 0xAA, 0x55, n)
    ser.write(header + samples)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("audio", type=pathlib.Path)
    ap.add_argument("--port", default=None)
    ap.add_argument("--baud", type=int, default=SERIAL_BAUD)
    args = ap.parse_args()

    if not args.audio.exists():
        raise SystemExit(f"File not found: {args.audio}")

    port = find_port(args.port)
    print(f"Audio : {args.audio}")
    print(f"Port  : {port} @ {args.baud}")
    print(f"Format: framed AA55 mono int16 @ {STREAM_RATE} Hz (loop)")
    print("Ctrl+C to stop")

    ser = serial.Serial(port, args.baud, timeout=0.1, write_timeout=3.0)
    time.sleep(1.2)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    # Read READY banner
    time.sleep(0.3)
    banner = ser.read(ser.in_waiting or 0)
    if banner:
        print(banner.decode("utf-8", "ignore").strip())

    proc = open_pcm_loop(args.audio)
    assert proc.stdout is not None

    bytes_sent = 0
    t0 = time.time()
    last_log = -1
    try:
        while True:
            raw = proc.stdout.read(PKT_SAMPLES * 2)
            if not raw:
                proc.kill()
                proc = open_pcm_loop(args.audio)
                assert proc.stdout is not None
                continue
            if len(raw) % 2:
                raw = raw[:-1]
            send_packet(ser, raw)
            bytes_sent += len(raw)

            # Pace near realtime so UART never floods into corruption
            elapsed = time.time() - t0
            expected = bytes_sent / (STREAM_RATE * 2)
            if expected > elapsed + 0.25:
                time.sleep(expected - elapsed - 0.08)

            sec = int(time.time() - t0)
            if sec != last_log and sec % 2 == 0:
                last_log = sec
                print(f"streaming... {bytes_sent/1024:.0f} KB", flush=True)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        try:
            proc.kill()
        except Exception:
            pass
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
