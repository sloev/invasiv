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
        const response = await fetch(url);
        const buffer = await response.arrayBuffer();
        const blob = new Blob([buffer], { type });
        return URL.createObjectURL(blob);
    }

    let ffmpeg = null;
    let handle = null;

    async function setup() {
        await init();
        handle = new WebHandle();
        await handle.start("the_canvas_id");

        ffmpeg = new FFmpeg();
        // Load from local lib/ folder
        const baseURL = './lib';
        await ffmpeg.load({
            classWorkerURL: await toBlobURL(`${baseURL}/worker.js`, 'text/javascript'),
            coreURL: await toBlobURL(`${baseURL}/ffmpeg-core.js`, 'text/javascript'),
            wasmURL: await toBlobURL(`${baseURL}/ffmpeg-core.wasm`, 'application/wasm'),
        });

        document.getElementById('loading-overlay').style.display = 'none';
        document.getElementById('controls').style.display = 'block';

        const uploader = document.getElementById('uploader');
        uploader.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;
            const data = new Uint8Array(await file.arrayBuffer());
            await ffmpeg.writeFile('input.mp4', data);
            handle.load_video('input.mp4');
        });

        let last_time = -1;
        setInterval(async () => {
            if (handle.is_load_clicked()) {
                uploader.click();
                handle.reset_load_clicked();
            }

            const current_time = handle.get_current_time();
            if (Math.abs(current_time - last_time) > 0.05) {
                last_time = current_time;
                try {
                    // Extract a single frame at current_time
                    await ffmpeg.exec([
                        '-ss', current_time.toString(),
                        '-i', 'input.mp4',
                        '-frames:v', '1',
                        '-s', '1280x720',
                        '-f', 'image2',
                        '-vcodec', 'rawvideo',
                        '-pix_fmt', 'rgba',
                        'out.raw'
                    ]);
                    const data = await ffmpeg.readFile('out.raw');
                    // We assume 1280x720 for now or similar. 
                    // Better: use ffprobe or similar to get size, but for now we try to push it.
                    // Since it's rawvideo rgba, we need to know the size.
                    // Let's use a fixed size for the preview for now or try to detect it.
                    // Actually, let's just use 640x360 for the preview to be safe and fast.
                    // await ffmpeg.exec(['-s', '640x360', ...])
                    handle.push_frame(data, 1280, 720); // Placeholder size, should be dynamic
                } catch (e) {
                    console.error("Frame extraction error:", e);
                }
            }
        }, 100);
    }

    setup().catch((err) => {
        console.error("FFmpeg Initialization Error:", err);
        document.getElementById('loading-overlay').innerHTML = "ERROR: " + err.message;
    });
</script>
