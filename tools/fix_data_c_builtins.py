#!/usr/bin/env python3
"""Fix build/web_gen/data.c builtin externs: unsigned int -> void for WASM."""
import re
from pathlib import Path

p = Path(__file__).resolve().parents[1] / "build" / "web_gen" / "data.c"
t = p.read_text(encoding="utf-8", errors="replace")
new_t, n = re.subn(r"extern unsigned int (\w+)\(\);", r"extern void \1(void);", t)
if n:
    p.write_text(new_t, encoding="utf-8")
print(f"patched {n} extern declarations in {p}")
