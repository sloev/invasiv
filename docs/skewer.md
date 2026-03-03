---
layout: default
title: "SKEWER // ONLINE_WARPER"
---

<section class="skewer-wasm">
    <h2 style="color: var(--accent); margin-bottom: 2rem;">SKEWER ONLINE</h2>
    
    <div id="loading-overlay" style="padding: 2rem; background: #111; color: var(--accent); margin-bottom: 2rem; border: 1px dashed var(--accent);">
        INITIALIZING FFmpeg.wasm... (This might take a moment)
    </div>

    <div id="controls" style="display:none; margin-bottom: 2rem;">
        <input type="file" id="uploader" accept="video/*" style="display:none;">
        <button onclick="document.getElementById('uploader').click()" class="btn" style="padding: 1rem 2rem; font-size: 0.8rem;">📁 LOAD VIDEO FOR PREVIEW</button>
    </div>

    <p style="margin-bottom: 2rem; color: var(--text-dim); font-size: 0.8rem;">
        <b>Instruction:</b> Use this online tool to mark your rhythmic events. The preview is powered by FFmpeg.wasm. 
        When done, copy the generated command to re-encode the file locally for Invasiv.
    </p>
    
    <div style="border: 2px solid var(--accent); background: #000; min-height: 600px; position: relative; overflow: hidden;">
        <canvas id="the_canvas_id" style="width: 100%; height: 100%;"></canvas>
    </div>
</section>

<!-- Load FFmpeg.wasm from CDN -->
<script src="https://unpkg.com/@ffmpeg/ffmpeg@0.12.10/dist/ffmpeg.min.js"></script>
<script src="https://unpkg.com/@ffmpeg/util@0.12.1/dist/index.min.js"></script>

<script type="module">
    import init, { WebHandle } from './skewer_wasm/skewer.js';
    
    const { createFFmpeg, fetchFile } = FFmpeg;
    const ffmpeg = createFFmpeg({ log: true });

    async function setup() {
        await init();
        const handle = new WebHandle();
        await handle.start("the_canvas_id");

        // Load FFmpeg
        await ffmpeg.load();
        document.getElementById('loading-overlay').style.display = 'none';
        document.getElementById('controls').style.display = 'block';

        const uploader = document.getElementById('uploader');
        uploader.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;
            
            // Write to FFmpeg virtual FS
            ffmpeg.FS('writeFile', 'input.mp4', await fetchFile(file));
            console.log("Video loaded into FFmpeg.wasm FS");
            
            // Logic to grab frames periodically or on-demand could go here
            // For now, we allow the UI to boot.
        });
    }

    setup().catch(console.error);
</script>
