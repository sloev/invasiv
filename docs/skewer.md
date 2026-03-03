---
layout: default
title: "SKEWER // ONLINE_WARPER"
---

<script src="./coi-serviceworker.js"></script>

<section class="skewer-wasm">
    <h2 style="color: var(--accent); margin-bottom: 2rem;">SKEWER ONLINE</h2>
    
    <div id="loading-overlay" style="padding: 2rem; background: #111; color: var(--accent); margin-bottom: 2rem; border: 1px dashed var(--accent); font-family: 'JetBrains Mono', monospace; font-size: 0.9rem; display: flex; flex-direction: column; align-items: center; gap: 1rem; position: relative; z-index: 10;">
        <div id="status-text">INITIALIZING SKEWER_WASM...</div>
        <div id="progress-container" style="width: 100%; max-width: 300px; height: 4px; background: #222; border-radius: 2px; overflow: hidden; display: none;">
            <div id="progress-bar" style="width: 0%; height: 100%; background: var(--accent); transition: width 0.3s ease;"></div>
        </div>
    </div>

    <div id="skewer-ui-root" style="opacity: 0.2; pointer-events: none; transition: opacity 0.5s ease;">
        <div id="controls" style="display:none; margin-bottom: 2rem;">
            <input type="file" id="uploader" accept="video/*" style="display:none;">
        </div>

        <p style="margin-bottom: 2rem; color: var(--text-dim); font-size: 0.8rem;">
            <b>Instruction:</b> Mark your rhythmic events on the timeline. The preview is powered by FFmpeg.wasm. 
            When done, copy the generated command to re-encode the file locally.
        </p>
        
        <div style="border: 2px solid var(--accent); background: #000; height: 70vh; max-height: 800px; position: relative; overflow: hidden; margin-bottom: 4rem;">
            <canvas id="the_canvas_id" style="width: 100%; height: 100%; display: block;"></canvas>
        </div>
    </div>
</section>

<script src="./lib/ffmpeg.js"></script>
<script src="./lib/util.js"></script>

<script type="module">
    import init, { WebHandle } from './skewer_wasm/skewer.js';
    
    const { FFmpeg } = window.FFmpegWASM;
    const { toBlobURL } = window.FFmpegUtil;

    const canvas = document.getElementById('the_canvas_id');
    const uploader = document.getElementById('uploader');
    const overlay = document.getElementById('loading-overlay');
    const statusText = document.getElementById('status-text');
    const progressContainer = document.getElementById('progress-container');
    const progressBar = document.getElementById('progress-bar');
    
    let ffmpeg = null;
    let handle = null;
    let ffmpegBusy = false;

    const setStatus = (msg, progress = -1) => {
        if (progress === -1 || progress === 0 || progress === 100) {
            console.log(`[WASM_STATUS] ${msg}`);
        }
        statusText.innerHTML = msg;
        if (progress >= 0) {
            progressContainer.style.display = 'block';
            progressBar.style.width = `${progress}%`;
        } else {
            progressContainer.style.display = 'none';
        }
    };

    // Trigger uploader on canvas click if WASM state requested it.
    canvas.addEventListener('click', () => {
        if (handle && handle.is_load_clicked()) {
            console.log("[WASM] Click detected on canvas, triggering hidden uploader");
            uploader.click();
            handle.reset_load_clicked();
        }
    });

    async function setup() {
        try {
            setStatus("BOOTING_RUST_ENGINE...");
            await init();
            handle = new WebHandle();
            await handle.start("the_canvas_id");
            
            setStatus("RUST_READY. LOADING_FFMPEG_CORE...", 0);

            ffmpeg = new FFmpeg();
            const baseURL = new URL('./lib', window.location.href).href;
            
            ffmpeg.on('log', ({ message }) => {
                console.log(`[FFmpeg] ${message}`);
            });

            setStatus("LOADING_FFMPEG_CORE...", 30);

            setStatus("INITIALIZING_VIRTUAL_HARDWARE...", 100);
            await ffmpeg.load({
                coreURL: `${baseURL}/ffmpeg-core.js`,
                wasmURL: `${baseURL}/ffmpeg-core.wasm`,
                classWorkerURL: `${baseURL}/814.ffmpeg.js`
            });

            setStatus("SYSTEM_ONLINE.");
            setTimeout(() => {
                overlay.style.display = 'none';
                document.getElementById('controls').style.display = 'block';
                const uiRoot = document.getElementById('skewer-ui-root');
                uiRoot.style.opacity = "1.0";
                uiRoot.style.pointerEvents = "all";
            }, 500);

        } catch (err) {
            console.error("[WASM_FATAL]", err);
            setStatus(`FATAL_ERROR: ${err.message}<br><br>Please ensure you are using a modern browser with Cross-Origin Isolation enabled.`);
            return;
        }

        uploader.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;
            
            console.log(`[WASM] File selected: ${file.name} (${(file.size/1024/1024).toFixed(2)} MB)`);
            
            // Get Metadata using Browser Video Element
            const video = document.createElement('video');
            video.preload = 'metadata';
            video.onloadedmetadata = () => {
                console.log(`[WASM] Metadata loaded: duration=${video.duration}s`);
                handle.set_duration(video.duration);
                window.URL.revokeObjectURL(video.src);
            };
            video.onerror = () => console.error("[WASM] Failed to load video metadata");
            video.src = URL.createObjectURL(file);

            try {
                const data = new Uint8Array(await file.arrayBuffer());
                await ffmpeg.writeFile('input.mp4', data);
                handle.load_video('input.mp4');
                console.log("[WASM] File written to virtual disk");
            } catch (err) {
                console.error("[WASM] Write error:", err);
            }
        });

        let last_time = -1;
        setInterval(async () => {
            if (ffmpegBusy || !ffmpeg || !ffmpeg.loaded) return;

            // Handle Export Request
            const cmd = handle.get_ffmpeg_command();
            if (cmd && cmd !== "") {
                console.log("[WASM] Export requested:", cmd);
                ffmpegBusy = true;
                setStatus("RENDERING_VIDEO...", 0);
                
                try {
                    // Parse the command for filter_complex
                    const filterMatch = cmd.match(/-filter_complex "([^"]+)"/);
                    const filter = filterMatch ? filterMatch[1] : "";
                    
                    if (!filter) {
                        console.error("[WASM] Failed to parse filter from command");
                        return;
                    }

                    await ffmpeg.exec([
                        '-i', 'input.mp4',
                        '-filter_complex', filter,
                        '-map', '[outv]',
                        '-an',
                        '-c:v', 'libx264',
                        '-crf', '23',
                        '-preset', 'veryfast',
                        '-pix_fmt', 'yuv420p',
                        'output.mp4'
                    ]);

                    const data = await ffmpeg.readFile('output.mp4');
                    const blob = new Blob([data.buffer], { type: 'video/mp4' });
                    const url = URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = 'warped_loop.mp4';
                    document.body.appendChild(a);
                    a.click();
                    document.body.removeChild(a);
                    URL.revokeObjectURL(url);
                    
                    setStatus("EXPORT_COMPLETE.");
                } catch (e) {
                    console.error("[WASM] Export failed:", e);
                    setStatus("EXPORT_FAILED: " + e.message);
                } finally {
                    ffmpegBusy = false;
                    setTimeout(() => setStatus("SYSTEM_ONLINE."), 3000);
                }
                return;
            }

            const current_time = handle.get_current_time();
            if (Math.abs(current_time - last_time) > 0.05) {
                ffmpegBusy = true;
                last_time = current_time;
                try {
                    // Extract a single frame at current_time
                    await ffmpeg.exec([
                        '-ss', current_time.toFixed(3),
                        '-i', 'input.mp4',
                        '-frames:v', '1',
                        '-s', '640x360',
                        '-f', 'image2',
                        '-vcodec', 'rawvideo',
                        '-pix_fmt', 'rgba',
                        'out.raw'
                    ]);
                    const data = await ffmpeg.readFile('out.raw');
                    handle.push_frame(data, 640, 360); 
                } catch (e) {
                    // console.warn("[WASM] Frame extraction failed");
                } finally {
                    ffmpegBusy = false;
                }
            }
        }, 100);
    }

    setup();
</script>
