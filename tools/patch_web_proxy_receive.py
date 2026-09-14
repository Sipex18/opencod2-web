#!/usr/bin/env python3
"""Patch __emscripten_receive_on_main_thread_js in the linked cod2.js.

The stock function throws ``TypeError: func is not a function`` when a
MAIN_THREAD_EM_ASM / proxied JS call targets a missing ASM_CONSTS entry or
proxiedFunctionTable slot, which aborts boot. Replace it with a version that
logs once and returns 0.

Must run after the link and before the imports are bound, so CMake calls this
as a POST_BUILD step. src/web/gl_proxied_frame_fix.js installs the same guard
at runtime when this patch does not apply.
"""
from pathlib import Path
import re
import sys

path = Path(sys.argv[1] if len(sys.argv) > 1 else "build-web-wsl/cod2.js")
text = path.read_text(encoding="utf-8", errors="replace")

new = """var __emscripten_receive_on_main_thread_js = (funcIndex, emAsmAddr, callingThread, numCallArgs, args) => {
  // cod2-proxy-receive-guard
  proxiedJSCallArgs.length = numCallArgs;
  var b = ((args) >> 3);
  for (var i = 0; i < numCallArgs; i++) {
    proxiedJSCallArgs[i] = GROWABLE_HEAP_F64()[b + i];
  }
  var addr = emAsmAddr | 0;
  var idx = funcIndex | 0;
  var func = addr ? ASM_CONSTS[addr] : proxiedFunctionTable[idx];
  if (typeof func !== "function") {
    // Log a few times — a silent permanent no-op hid graphics/canvas failures.
    var nlog = (__emscripten_receive_on_main_thread_js._cod2LogCount | 0);
    if (nlog < 8) {
      __emscripten_receive_on_main_thread_js._cod2LogCount = nlog + 1;
      console.error("[cod2-proxy] missing main-thread target", {
        funcIndex: idx,
        emAsmAddr: addr,
        asmConsts: typeof ASM_CONSTS !== "undefined" ? Object.keys(ASM_CONSTS).length : -1,
        tableLen: typeof proxiedFunctionTable !== "undefined" ? proxiedFunctionTable.length : -1
      });
    }
    return 0;
  }
  PThread.currentProxiedOperationCallerThread = callingThread;
  var rtn = func(...proxiedJSCallArgs);
  PThread.currentProxiedOperationCallerThread = 0;
  return rtn;
};
__emscripten_receive_on_main_thread_js.__cod2ProxyReceiveGuard = true;"""

# Replace the stock (or already guarded) receive function. Emscripten leaves a
# comment and an extra assert between the thread bookkeeping and the return, so
# the body has to be matched loosely rather than as one fixed block.
pat = re.compile(
    r"var __emscripten_receive_on_main_thread_js = \(funcIndex, emAsmAddr, callingThread, numCallArgs, args\) => \{"
    r".*?PThread\.currentProxiedOperationCallerThread = 0;"
    r".*?return rtn;\s*"
    r"\};",
    re.S,
)
m = pat.search(text)
if not m:
    print("PATCH_FAIL: receive function not found (continuing; gl_proxied_frame_fix.js guard still active)")
    sys.exit(0)

text = text[: m.start()] + new + text[m.end() :]
path.write_text(text, encoding="utf-8")
print("patched", path)
