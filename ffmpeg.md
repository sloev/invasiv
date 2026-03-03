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
    - **Root Cause:** Browsers block cross-origin Workers unless they are explicitly permitted via `import { ... }` or proxied through a Blob. ESM.sh's worker resolution didn't bypass the origin check.

- [X] **Attempt 3: Revert to UMD + toBlobURL for classWorkerURL**
    - **Result:** Failed (reported by user). Still getting `SecurityError`.
    - **Root Cause:** The `classWorkerURL` itself, even if converted to a Blob, might be trying to import other cross-origin scripts or the `toBlobURL` helper didn't successfully mask the origin.

- [ ] **Attempt 4: Proxy the Worker script via a local Blob wrapper**
    - **Concept:** Create a small local script that `importScripts` the CDN worker, then load that via `URL.createObjectURL`.

- [ ] **Attempt 5: Version Downgrade to 0.11**
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
