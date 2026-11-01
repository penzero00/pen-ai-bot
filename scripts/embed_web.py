#!/usr/bin/env python3
"""
embed_web.py — Mirror web-dashboard/ into firmware/data/ for ESP32-S3 LittleFS.
Ensures 100% parity between local preview and flashed LittleFS assets.

Usage (standalone):
    python scripts/embed_web.py

Usage (PlatformIO pre-action in platformio.ini):
    extra_scripts = pre:scripts/embed_web.py
"""

import os
import shutil

try:
    Import("env")  # type: ignore  # noqa: F821  — SCons injected global
    PROJECT_ROOT = env.subst("$PROJECT_DIR")
    is_pio = True
except Exception:
    is_pio = False
    if "__file__" in globals():
        SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
        PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
    else:
        PROJECT_ROOT = os.getcwd()

SRC_DIR = os.path.join(PROJECT_ROOT, "web-dashboard")
DST_DIR = os.path.join(PROJECT_ROOT, "firmware", "data")


def main():
    """Recursively mirror web-dashboard/ -> firmware/data/"""
    if not os.path.isdir(SRC_DIR):
        print(f"[embed_web] ERROR: Source folder not found: {SRC_DIR}")
        return

    os.makedirs(DST_DIR, exist_ok=True)

    # Sync files
    total_copied = 0
    total_bytes = 0
    for root, dirs, files in os.walk(SRC_DIR):
        rel_path = os.path.relpath(root, SRC_DIR)
        target_dir = os.path.join(DST_DIR, rel_path)
        os.makedirs(target_dir, exist_ok=True)

        for file in files:
            src_file = os.path.join(root, file)
            dst_file = os.path.join(target_dir, file)
            shutil.copy2(src_file, dst_file)
            total_copied += 1
            total_bytes += os.path.getsize(dst_file)

    print(
        f"[embed_web] Successfully mirrored {total_copied} file(s) ({total_bytes / 1024:.1f} KB) "
        f"from web-dashboard/ -> firmware/data/"
    )


if is_pio:
    env.AddPreAction("buildfs", lambda *a, **kw: main())  # type: ignore  # noqa: F821
    print("[embed_web] Registered PlatformIO pre:buildfs hook.")

if __name__ == "__main__":
    main()
