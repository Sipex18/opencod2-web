#!/usr/bin/env bash
set -euo pipefail
source "$HOME/emsdk/emsdk_env.sh"
export EM_CACHE="${EM_CACHE:-$HOME/emscripten_cache}"
export TMPDIR=/tmp
cd /mnt/c/Users/sipex/Desktop/opencod2-main

which emcc
emcc -v 2>&1 | head -3

for i in $(seq 1 40); do
  echo "===== LINK ROUND $i ====="
  set +e
  cmake --build build-web-wsl -j 6 > /tmp/link_round.log 2>&1
  rc=$?
  set -e
  tail -80 /tmp/link_round.log | tee -a "$HOME/cod2-web-build2.log"
  if [[ $rc -eq 0 ]]; then
    echo BUILD_OK
    ls -lah build-web-wsl/cod2.html build-web-wsl/cod2.js build-web-wsl/cod2.wasm build-web-wsl/cod2_fs.js
    exit 0
  fi

  sym=$(grep -oP 'wasm-ld: error: function signature mismatch: \K\S+' /tmp/link_round.log | head -1 || true)
  if [[ -z "${sym}" ]]; then
    echo "No signature mismatch error to auto-fix; dumping tail:"
    tail -50 /tmp/link_round.log
    exit 2
  fi

  echo "Mismatch ERROR on ${sym}"
  grep -A2 "function signature mismatch: ${sym}" /tmp/link_round.log | head -6 || true

  void_file=$(awk -v s="$sym" '
    $0 ~ "wasm-ld: error: function signature mismatch: "s {grab=1; next}
    grab && /defined as/ {print; if (++n==2) exit}
  ' /tmp/link_round.log | grep -F -- '-> void' \
    | grep -oP 'CMakeFiles/cod2\.dir/\K[^ ]+\.c' | head -1 || true)

  if [[ -z "${void_file}" ]]; then
    echo "No void side; manual needed"
    exit 3
  fi

  src="src/${void_file#src/}"
  if [[ ! -f "${src}" ]]; then src="${void_file}"; fi
  if [[ ! -f "${src}" ]]; then
    echo "cannot find ${void_file}"
    exit 4
  fi

  echo "Patching extern in ${src} for ${sym}"
  python3 - "$src" "$sym" <<'PY'
import re, sys
from pathlib import Path
p = Path(sys.argv[1])
sym = sys.argv[2]
t = p.read_text(encoding="utf-8", errors="replace")
old = f"extern void {sym}("
new = f"extern int {sym}("
if old in t:
    t = t.replace(old, new, 1)
else:
    t2, n = re.subn(rf"extern\s+void\s+{re.escape(sym)}\s*\(", f"extern int {sym}(", t, count=1)
    if n == 0:
        print("pattern not found in", p)
        sys.exit(5)
    t = t2
p.write_text(t, encoding="utf-8")
print("patched", p)
PY

  rm -f "build-web-wsl/CMakeFiles/cod2.dir/${void_file}.o"
done

echo "Too many rounds"
exit 1
