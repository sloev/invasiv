---
layout: default
title: "SKEWER // ONLINE_WARPER"
---

<section class="skewer-wasm">
    <h2 style="color: var(--accent); margin-bottom: 2rem;">SKEWER ONLINE</h2>
    <p style="margin-bottom: 2rem; color: var(--text-dim);">
        Prepare your rhythmic loops directly in the browser. 
        Note: Web export might be limited by browser filesystem access. For full power, use the native binary.
    </p>
    
    <div style="border: 2px solid var(--accent); background: #000; height: 600px; position: relative;">
        <canvas id="the_canvas_id" style="width: 100%; height: 100%;"></canvas>
    </div>
</section>

<!-- Load the WASM glue code -->
<script type="module">
    import init from './skewer_wasm/skewer.js';
    init().catch((error) => {
        if (!error.message.startsWith("Using exceptions for control flow,")) {
            throw error;
        }
    });
</script>
