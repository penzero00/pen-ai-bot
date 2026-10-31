#!/usr/bin/env python3
"""
fix_ide_intellisense.py — Synchronize compile_commands.json, .clangd, and .vscode/settings.json
with PlatformIO's exact ESP32-S3 Arduino framework paths and Xtensa sysroot headers.
"""

import json
import os
import sys

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
IDEDATA_PATH = os.path.join(PROJECT_ROOT, ".pio", "build", "esp32-s3-devkitc-1", "idedata.json")
COMPILE_DB_PATH = os.path.join(PROJECT_ROOT, "compile_commands.json")
CLANGD_PATH = os.path.join(PROJECT_ROOT, ".clangd")
SETTINGS_PATH = os.path.join(PROJECT_ROOT, ".vscode", "settings.json")

TOOLCHAIN_SYSROOT = [
    "C:/Users/user/.platformio/packages/toolchain-xtensa-esp-elf/xtensa-esp-elf/include",
    "C:/Users/user/.platformio/packages/toolchain-xtensa-esp-elf/xtensa-esp-elf/include/c++/14.2.0",
    "C:/Users/user/.platformio/packages/toolchain-xtensa-esp-elf/xtensa-esp-elf/include/c++/14.2.0/xtensa-esp-elf",
    "C:/Users/user/.platformio/packages/toolchain-xtensa-esp-elf/lib/gcc/xtensa-esp-elf/14.2.0/include",
    "C:/Users/user/.platformio/packages/toolchain-xtensa-esp-elf/lib/gcc/xtensa-esp-elf/14.2.0/include-fixed",
]


def main():
    if not os.path.isfile(IDEDATA_PATH):
        print(f"Error: {IDEDATA_PATH} not found.")
        return 1

    with open(IDEDATA_PATH, "r", encoding="utf-8") as f:
        idedata = json.load(f)

    includes = idedata.get("includes", {}).get("build", [])
    defines = idedata.get("defines", [])
    cxx_path = idedata.get("cxx_path", "xtensa-esp32s3-elf-g++")
    cc_path = idedata.get("cc_path", "xtensa-esp32s3-elf-gcc")

    # 1. Update compile_commands.json
    compile_db = []
    if os.path.isfile(COMPILE_DB_PATH):
        with open(COMPILE_DB_PATH, "r", encoding="utf-8") as f:
            try:
                compile_db = json.load(f)
            except Exception:
                compile_db = []

    sysroot_flags = " ".join([f'-isystem "{p}"' for p in TOOLCHAIN_SYSROOT if os.path.exists(p)])
    inc_flags = " ".join([f'-I"{inc}"' for inc in includes])
    def_flags = " ".join([f'-D{d}' if '=' in d else f'-D{d}' for d in defines])
    base_flags = "-mlongcalls -mdisable-hardware-atomics -gdwarf-4 -ggdb -std=gnu++2a -Wall -Wextra"

    main_cpp = os.path.join(PROJECT_ROOT, "firmware", "src", "main.cpp")
    qrcode_c = os.path.join(PROJECT_ROOT, "firmware", "src", "qrcode.c")

    main_entry = {
        "directory": PROJECT_ROOT,
        "command": f'{cxx_path} -o .pio\\build\\esp32-s3-devkitc-1\\src\\main.cpp.o -c {base_flags} {def_flags} {sysroot_flags} {inc_flags} "{main_cpp}"',
        "file": main_cpp,
        "output": ".pio\\build\\esp32-s3-devkitc-1\\src\\main.cpp.o"
    }

    qrcode_entry = {
        "directory": PROJECT_ROOT,
        "command": f'{cc_path} -o .pio\\build\\esp32-s3-devkitc-1\\src\\qrcode.c.o -c -std=gnu17 {def_flags} {sysroot_flags} {inc_flags} "{qrcode_c}"',
        "file": qrcode_c,
        "output": ".pio\\build\\esp32-s3-devkitc-1\\src\\qrcode.c.o"
    }

    filtered_db = [
        e for e in compile_db
        if not e.get("file", "").endswith("main.cpp") and not e.get("file", "").endswith("qrcode.c")
    ]
    filtered_db.insert(0, qrcode_entry)
    filtered_db.insert(0, main_entry)

    with open(COMPILE_DB_PATH, "w", encoding="utf-8") as f:
        json.dump(filtered_db, f, indent=4)
    print(f"Updated {COMPILE_DB_PATH} with main.cpp and qrcode.c entries ({len(filtered_db)} total entries).")

    # 2. Generate .clangd configuration
    clangd_content = "CompileFlags:\n  Add:\n"
    clangd_content += '    - "-std=gnu++20"\n'

    for sys_inc in TOOLCHAIN_SYSROOT:
        if os.path.exists(sys_inc):
            clangd_content += f'    - "-isystem"\n'
            clangd_content += f'    - "{sys_inc}"\n'

    for d in defines:
        safe_d = d.replace('"', '\\"')
        clangd_content += f'    - "-D{safe_d}"\n'

    for inc in includes:
        norm_inc = inc.replace("\\", "/")
        clangd_content += f'    - "-I{norm_inc}"\n'

    clangd_content += """  Remove:
    - "-mlongcalls"
    - "-mdisable-hardware-atomics"
    - "-fno-tree-switch-conversion"
    - "-fstrict-volatile-bitfields"
Diagnostics:
  Suppress:
    - "-Wdeprecated-declarations"
    - "-Wvolatile"
"""

    with open(CLANGD_PATH, "w", encoding="utf-8") as f:
        f.write(clangd_content)
    print(f"Generated {CLANGD_PATH} with sysroot and project include/define flags.")

    # 3. Generate .vscode/settings.json
    all_includes = [p for p in TOOLCHAIN_SYSROOT if os.path.exists(p)] + [inc.replace("\\", "/") for inc in includes]
    settings = {
        "C_Cpp.default.includePath": all_includes,
        "C_Cpp.default.defines": defines,
        "C_Cpp.default.cppStandard": "gnu++20",
        "C_Cpp.default.cStandard": "gnu17",
        "C_Cpp.intelliSenseEngine": "default",
        "clangd.arguments": [
            "--compile-commands-dir=" + PROJECT_ROOT.replace("\\", "/"),
            "--query-driver=C:/Users/user/.platformio/packages/toolchain-xtensa-esp-elf/bin/*",
            "--background-index",
            "--header-insertion=never"
        ]
    }

    os.makedirs(os.path.dirname(SETTINGS_PATH), exist_ok=True)
    with open(SETTINGS_PATH, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=4)
    print(f"Generated {SETTINGS_PATH}.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
