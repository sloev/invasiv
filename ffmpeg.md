# FFmpeg Strategy & Tracked Attempts

## Reference Designs
- **Official FFmpeg.wasm 0.12 ESM Sample:** Uses `toBlobURL` for `coreURL`, `wasmURL`, and `workerURL`.
- **coi-serviceworker:** Required for `SharedArrayBuffer` support on GitHub Pages.
- **Chrome Security Requirements:** Workers must be same-origin OR served with specific headers.

## Tracked Attempts (Skewer WASM)

- [X] **Attempt 1: Local UMD loading (initial)**
    - **Result:** Failed. Fatal error `814.ffmpeg.js` could not be accessed from origin.
    - **Root Cause:** FFmpeg.wasm 0.12 internal chunks are not being found or are blocked by CORS when loaded as a script.

- [X] **Attempt 2: CDN-based ESM loading (esm.sh)**
    - **Result:** Failed. `SecurityError: Failed to construct 'Worker'`.
    - **Console Output:**
      ```
      skewer:263 [WASM_STATUS] BOOTING_RUST_ENGINE...
      skewer:263 [WASM_STATUS] RUST_READY. LOADING_FFMPEG_CORE...
      skewer:263 [WASM_STATUS] INITIALIZING_VIRTUAL_HARDWARE...
      skewer:317 [WASM_FATAL] SecurityError: Failed to construct 'Worker': Script at 'https://esm.sh/@ffmpeg/ffmpeg@0.12.10/es2022/worker.js' cannot be accessed from origin 'https://invasiv.github.io'.
          at d.load (classes.js:109:17)
          at setup (skewer:305:26)
      setup @ skewer:317
      await in setup
      (anonymous) @ skewer:380
      skewer:263 [WASM_STATUS] FATAL_ERROR: Failed to construct 'Worker': Script at 'https://esm.sh/@ffmpeg/ffmpeg@0.12.10/es2022/worker.js' cannot be accessed from origin 'https://invasiv.github.io'.<br><br>Please ensure you are using a modern browser with Cross-Origin Isolation enabled.
      ```
    - **Root Cause:** Browsers block cross-origin Workers unless they are explicitly permitted via `import { ... }` or proxied through a Blob. ESM.sh's worker resolution didn't bypass the origin check.

- [X] **Attempt 3: Revert to UMD + toBlobURL for classWorkerURL**
    - **Result:** Failed. `Error: failed to import ffmpeg-core.js` and `FATAL_ERROR: undefined`.
    - **Root Cause:** In FFmpeg.wasm 0.12, dynamic imports inside the worker can fail when given a `blob:` URL for the core, especially when CORS is not the actual issue. Also, the `0.12.10` UMD build contains a hardcoded bug (`file:///home/jeromewu/...`), which was likely causing issues.

- [X] **Attempt 4: Same-Origin Direct URLs (No toBlobURL)**
    - **Concept:** Since the GitHub Action downloads the files into `docs/lib`, they are served from the same origin as the page. `toBlobURL` is strictly for bypassing cross-origin CDN constraints. We can pass the relative paths directly. We also downgrade `@ffmpeg/ffmpeg` UMD to `0.12.6` to avoid the `file:///` compilation bug found in `0.12.10`.
    - **Result:** Failed. `Error: failed to import ffmpeg-core.js`.
    - **Root Cause:** When `coreURL` is passed as a relative path like `./lib/ffmpeg-core.js`, it is evaluated *inside the Web Worker*. Since the worker's URL is `./lib/814.ffmpeg.js`, it resolves the relative path to `./lib/lib/ffmpeg-core.js`, which is a 404.

- [ ] **Attempt 5: Same-Origin Absolute URLs**
    - **Concept:** Ensure the `baseURL` is absolute using `new URL('./lib', window.location.href).href`. This creates a fully qualified URL (e.g., `https://invasiv.github.io/lib/...`) that the worker can unambiguously resolve, regardless of its own path context.
    - **Status:** Implemented, waiting for validation.

- [ ] **Attempt 6: Version Downgrade to 0.11**
    - **Concept:** 0.12 is significantly more complex with its worker/chunk architecture. 0.11 might be more stable for this simple use case.

## Tracked Attempts (Skewer Native)

- [X] **Attempt 1: Basic Command Spawning**
    - **Result:** No video shows up. No console output.
    - **Root Cause:** Lack of logging and hardcoded resolution expectations (`1280x720` vs actual).

- [ ] **Attempt 2: Added `log` crate and forced `640x360` resolution**
    - **Status:** Implemented, waiting for execution/validation.
    - **Concept:** Force FFmpeg to output a known size and capture `stderr` to see why it's failing.

## Current Strategy
1. Investigate if `coi-serviceworker.js` is actually being loaded and active (check `window.crossOriginIsolated`).
2. If WASM remains blocked, try the "Blob Proxy" pattern for the worker.
3. For Native, verify FFmpeg/FFprobe presence in PATH and check the new logs.
