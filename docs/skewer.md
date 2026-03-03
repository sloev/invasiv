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
        <button id="load-btn" class="btn" style="padding: 1rem 2rem; font-size: 0.8rem;">📁 LOAD VIDEO FOR PREVIEW</button>
    </div>

    <p style="margin-bottom: 2rem; color: var(--text-dim); font-size: 0.8rem;">
        <b>Instruction:</b> Use this online tool to mark your rhythmic events. The preview is powered by FFmpeg.wasm. 
        When done, copy the generated command to re-encode the file locally for Invasiv.
    </p>
    
    <div style="border: 2px solid var(--accent); background: #000; min-height: 600px; position: relative; overflow: hidden;">
        <canvas id="the_canvas_id" style="width: 100%; height: 100%;"></canvas>
    </div>
</section>

<script type="module">
    import { FFmpeg } from 'https://unpkg.com/@ffmpeg/ffmpeg@0.12.10/dist/esm/index.js';
    import { toBlobURL } from 'https://unpkg.com/@ffmpeg/util@0.12.1/dist/esm/index.js';
    import init, { WebHandle } from './skewer_wasm/skewer.js';
    
    let ffmpeg = null;

    async function setup() {
        // Load SKEWER WASM
        await init();
        const handle = new WebHandle();
        await handle.start("the_canvas_id");

        // Load FFmpeg WASM with robust cross-origin workarounds
        ffmpeg = new FFmpeg();
        const baseURL = 'https://unpkg.com/@ffmpeg/core@0.12.6/dist/esm';
        const ffmpegURL = 'https://unpkg.com/@ffmpeg/ffmpeg@0.12.10/dist/esm';
        
        await ffmpeg.load({
            coreURL: await toBlobURL(`${baseURL}/ffmpeg-core.js`, 'text/javascript'),
            wasmURL: await toBlobURL(`${baseURL}/ffmpeg-core.wasm`, 'application/wasm'),
            // Explicitly load worker via Blob to bypass origin security errors
            workerURL: await toBlobURL(`${ffmpegURL}/worker.js`, 'text/javascript'),
        });

        document.getElementById('loading-overlay').style.display = 'none';
        document.getElementById('controls').style.display = 'block';

        const uploader = document.getElementById('uploader');
        const loadBtn = document.getElementById('load-btn');
        
        loadBtn.addEventListener('click', () => uploader.click());
        
        uploader.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;
            
            const data = new Uint8Array(await file.arrayBuffer());
            await ffmpeg.writeFile('input.mp4', data);
            console.log("Video loaded into FFmpeg.wasm virtual FS");
        });
    }

    setup().catch((err) => {
        console.error("FFmpeg Initialization Error:", err);
        document.getElementById('loading-overlay').innerHTML = "ERROR: " + err.message;
    });
</script>
