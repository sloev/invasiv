---
layout: default
title: "SKEWER // ONLINE_WARPER"
---

<section class="skewer-wasm">
    <h2 style="color: var(--accent); margin-bottom: 2rem;">SKEWER ONLINE</h2>
    <p style="margin-bottom: 2rem; color: var(--text-dim);">
        Prepare your rhythmic loops directly in the browser. 
        <b>Note:</b> Web re-encoding is technically restricted. Use this to mark your beats, then copy the generated command to run locally with FFmpeg.
    </p>
    
    <div style="border: 2px solid var(--accent); background: #000; height: 600px; position: relative; overflow: hidden;">
        <canvas id="the_canvas_id" style="width: 100%; height: 100%;"></canvas>
    </div>
</section>

<!-- Load the WASM glue code -->
<script type="module">
    import init, { WebHandle } from './skewer_wasm/skewer.js';
    
    async function run() {
        await init();
        const handle = new WebHandle();
        await handle.start("the_canvas_id");
    }

    run().catch((error) => {
        if (!error.message.startsWith("Using exceptions for control flow,")) {
            console.error(error);
        }
    });
</script>
