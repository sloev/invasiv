---
layout: default
title: "SKEWER // ONLINE_WARPER"
---

<script src="./coi-serviceworker.js"></script>

<section class="skewer-wasm">
    <h2 style="color: var(--accent); margin-bottom: 2rem;">SKEWER ONLINE</h2>
    
    <div id="loading-overlay" style="padding: 2rem; background: #111; color: var(--accent); margin-bottom: 2rem; border: 1px dashed var(--accent);">
        INITIALIZING SKEWER_WASM + FFmpeg.wasm...
    </div>

    <div id="controls" style="display:none; margin-bottom: 2rem;">
        <input type="file" id="uploader" accept="video/*" style="display:none;">
    </div>

    <p style="margin-bottom: 2rem; color: var(--text-dim); font-size: 0.8rem;">
        <b>Instruction:</b> Mark your rhythmic events on the timeline. The preview is powered by FFmpeg.wasm. 
        When done, copy the generated command to re-encode the file locally.
    </p>
    
    <div style="border: 2px solid var(--accent); background: #000; min-height: 600px; position: relative; overflow: hidden;">
        <canvas id="the_canvas_id" style="width: 100%; height: 100%;"></canvas>
    </div>
</section>

<script type="module">
    import { FFmpeg } from './lib/index.js';
    import init, { WebHandle } from './skewer_wasm/skewer.js';
    
    // Inline toBlobURL to avoid dependency on missing export in util.js
    async function toBlobURL(url, type) {
        console.log(`[WASM] Fetching blob URL for: ${url}`);
        const response = await fetch(url);
        const buffer = await response.arrayBuffer();
        const blob = new Blob([buffer], { type });
        return URL.createObjectURL(blob);
    }

    const canvas = document.getElementById('the_canvas_id');
    const uploader = document.getElementById('uploader');
    
    let ffmpeg = null;
    let handle = null;
    let ffmpegBusy = false;

    // Trigger uploader on canvas click if WASM state requested it.
    canvas.addEventListener('click', () => {
        console.log("[WASM] Canvas clicked");
        if (handle && handle.is_load_clicked()) {
            console.log("[WASM] Load requested by Rust, triggering uploader");
            uploader.click();
            handle.reset_load_clicked();
        }
    });

    async function setup() {
        console.log("[WASM] Starting setup...");
        await init();
        handle = new WebHandle();
        await handle.start("the_canvas_id");
        console.log("[WASM] Rust engine started");

        ffmpeg = new FFmpeg();
        const baseURL = './lib';
        
        ffmpeg.on('log', ({ message }) => {
            // console.log(`[FFmpeg] ${message}`);
        });

        console.log("[WASM] Loading FFmpeg...");
        await ffmpeg.load({
            classWorkerURL: await toBlobURL(`${baseURL}/worker.js`, 'text/javascript'),
            coreURL: await toBlobURL(`${baseURL}/ffmpeg-core.js`, 'text/javascript'),
            wasmURL: await toBlobURL(`${baseURL}/ffmpeg-core.wasm`, 'application/wasm'),
        });
        console.log("[WASM] FFmpeg Loaded");

        document.getElementById('loading-overlay').style.display = 'none';
        document.getElementById('controls').style.display = 'block';

        uploader.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;
            
            console.log(`[WASM] File selected: ${file.name} (${file.size} bytes)`);
            
            // Get Metadata using Browser Video Element
            const video = document.createElement('video');
            video.preload = 'metadata';
            video.onloadedmetadata = () => {
                console.log(`[WASM] Video duration detected: ${video.duration}s`);
                handle.set_duration(video.duration);
                window.URL.revokeObjectURL(video.src);
            };
            video.src = URL.createObjectURL(file);

            const data = new Uint8Array(await file.arrayBuffer());
            await ffmpeg.writeFile('input.mp4', data);
            handle.load_video('input.mp4');
            console.log("[WASM] Video transferred to FFmpeg filesystem");
        });

        let last_time = -1;
        setInterval(async () => {
            if (ffmpegBusy || !ffmpeg) return;

            const current_time = handle.get_current_time();
            if (Math.abs(current_time - last_time) > 0.05) {
                ffmpegBusy = true;
                last_time = current_time;
                console.log(`[WASM] Seeking to ${current_time.toFixed(2)}s`);
                try {
                    // Extract a single frame at current_time (optimized size for preview)
                    await ffmpeg.exec([
                        '-ss', current_time.toString(),
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
                    // console.log("[WASM] Frame pushed to Rust");
                } catch (e) {
                    // console.error("[WASM] Frame extraction error:", e);
                }
                ffmpegBusy = false;
            }
        }, 100);
    }

    setup().catch((err) => {
        console.error("[WASM] Global Setup Error:", err);
        document.getElementById('loading-overlay').innerHTML = "ERROR: " + err.message;
    });
</script>
