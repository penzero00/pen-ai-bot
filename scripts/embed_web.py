#!/usr/bin/env python3
"""
embed_web.py — Copy the source web-dashboard into the LittleFS data folder
before PlatformIO flashes the filesystem image to the ESP32-S3.

Usage (standalone):
    python scripts/embed_web.py

Usage (PlatformIO pre-action, add to platformio.ini):
    extra_scripts = pre:scripts/embed_web.py
"""

import shutil
import os

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

SRC_HTML = os.path.join(PROJECT_ROOT, "web-dashboard", "index.html")
DST_DIR = os.path.join(PROJECT_ROOT, "firmware", "data")
DST_HTML = os.path.join(DST_DIR, "index.html")


def main():
    """Copy web-dashboard/index.html -> firmware/data/index.html"""
    if not os.path.isfile(SRC_HTML):
        print(f"[embed_web] ERROR: Source not found: {SRC_HTML}")
        return

    os.makedirs(DST_DIR, exist_ok=True)
    shutil.copy2(SRC_HTML, DST_HTML)
    size_kb = os.path.getsize(DST_HTML) / 1024
    print(f"[embed_web] Copied web-dashboard/index.html -> firmware/data/index.html ({size_kb:.1f} KB)")


if is_pio:
    env.AddPreAction("buildfs", lambda *a, **kw: main())  # type: ignore  # noqa: F821
    print("[embed_web] Registered PlatformIO pre:buildfs hook.")

if __name__ == "__main__":
    main()
