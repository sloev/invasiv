# Beat Tracker Research & Implementation

## Objective
Implement a real-time beat tracker running on the master node to analyze an incoming audio signal for **beat onsets** and **BPM tempo**. The data will dynamically adjust the master metronome (and peers via network) by skewing the phase (The '1' beat) and updating the BPM, while allowing manual latency compensation.

---

## Literature Review & State of the Art Validation (2024-2026)

To ensure we are building the absolute most accurate system, we reviewed the latest literature, specifically focusing on the performance of models like **BeatNet** against traditional methods.

### 1. The Real-Time Problem (Online vs. Offline)
*   **The "Madmom" Context:** Older highly accurate systems (like Madmom's offline mode) relied on **Bidirectional** processing and **Viterbi Decoding**. They analyzed the entire audio file at once, looking into the future to correct past mistakes. This is impossible for live audio.
*   **The Challenge:** True real-time ("online" or "causal") tracking must make decisions using only past and present audio. Historically, this meant a massive drop in accuracy and a susceptibility to "jitter" from syncopation or drum fills.

### 2. The Modern Solution: BeatNet (CRNN + Particle Filtering)
Recent literature (e.g., *BeatNet: CRNN and Particle Filtering for Online Joint Beat Downbeat and Meter Tracking*) proves that we can achieve near-offline accuracy in a strictly real-time setting. 
*   **Feature Extraction (CRNN):** Instead of simple energy peaks or algorithmic novelty functions, it uses a Convolutional Recurrent Neural Network to learn complex, genre-agnostic features directly from the spectrogram.
*   **Inference (Particle Filtering):** Instead of the computationally heavy and non-causal Viterbi algorithm, BeatNet uses **Particle Filtering** (a form of sequential Monte Carlo method). This acts as a highly advanced Phase-Locked Loop (PLL). It maintains a "belief state" of multiple possible tempos and phases, allowing it to stubbornly lock onto the true beat even through noise and complex rhythms, all in real-time.

*   **Benchmarks:** BeatNet consistently outperforms traditional 1D State Space HMMs and causal CNNs (scoring ~80.64 F-measure on real-time GTZAN datasets, which is exceptional for causal tracking).

---

## Implementation Blueprint (ONNX Native)

Based on the literature, **BeatNet** is the exact mathematical model we want. To run it efficiently in C++ on older hardware (without Python bloat), we will use an **ONNX Native** architecture.

### 1. The Architecture
1.  **Offline Export:** Use a Python script to convert BeatNet's pre-trained PyTorch weights (`.pt`) into a static `beatnet.onnx` file.
2.  **C++ Runtime:** Integrate **ONNX Runtime (CPU)** into the master node. It will execute the CRNN using highly optimized SIMD instructions in under 10ms.
3.  **Particle Filter:** Implement the 1D Particle Filter in C++ to decode the neural activations into stable, real-time beat timestamps.

### 2. The "Ultimate Precision" Trick: Lookahead Buffering
While BeatNet is SOTA for zero-latency, our metronome application allows us a unique advantage: **Latency Compensation**.
*   Instead of demanding an instant answer, the master node will buffer ~50ms of audio before feeding it to the ONNX model. 
*   This tiny glimpse into the "future" allows the Particle Filter to drastically reduce false positives (pushing the accuracy even closer to Madmom's offline exactness).
*   **Metronome Phase-Shift:** Because the tracker is now exactly 50ms late, we subtract that exact amount from the detected timestamp before syncing `Metronome.h`. The metronome clicks in perfect real-time alignment.

### 3. The Sync Logic (`Metronome.h`)
*   **BPM Smoothing:** `smoothed_bpm = (old_bpm * 0.9) + (onnx_bpm * 0.1)`.
*   **Phase Skewing (Soft-Sync):** Calculate `delta = (actual_beat_time - lookahead_delay - hardware_latency) - expected_beat_time`. Apply `delta * 0.25` to smoothly skew the metronome without glitches.

---

## Research Checklist (Append-Only)

- [x] **Review Past Implementation (`beatape`):** Understood the `madmom` pipeline.
- [x] **Literature Review & SOTA Validation:** Reviewed ResearchGate and audioXpress literature. Confirmed that **BeatNet (CRNN + Particle Filtering)** is the proven State-of-the-Art for causal (real-time) tracking, overcoming the limitations of traditional HMMs and algorithmic novelty functions.
- [x] **Select the Exact Path:** **ONNX Native with BeatNet weights + Lookahead Buffering**. This combines modern causal network efficiency with a small buffer to maximize precision.
- [x] **Model Extraction Strategy:** Convert BeatNet `.pt` to `.onnx`.
- [x] **C++ Inference Strategy:** `Ort::Session` on a background thread.
- [x] **Specific Phase & Tempo Sync Strategy:** Soft-Sync skewing formula accounting for the Lookahead buffer.

## Draft for `FEATURES.md`
*(Pending user approval before modification)*
- [ ] **ONNX Model Export:** Create a standalone Python script to export BeatNet's pre-trained PyTorch weights to a static `beatnet.onnx` model file for native C++ inference.
- [ ] **Lookahead Neural Inference:** Integrate ONNX Runtime (C++ API). Run the BeatNet model on a dedicated background thread using a small lookahead buffer (~50ms) to maximize precision, extracting beat probabilities from spectrograms.
- [ ] **Particle Filter Decoding:** Implement a lightweight particle filter (based on the BeatNet paper) in C++ to decode neural activations into stable BPM and beat timestamps.
- [ ] **Tempo & Phase Soft-Sync:** Implement a smoothing mechanism in `Metronome.h` to skew the metronome phase (`delta * 0.25`), subtracting the known lookahead and hardware latency to achieve perfect real-time alignment.
- [ ] **Latency Calibration UI:** Add a slider to the GUI to manually offset timestamps (-500ms to +500ms) compensating for hardware pipeline latency.
- [ ] **Network Broadcast:** Transmit the smoothed BPM and phase offsets to peer nodes via `StateManager` to synchronize the global network clock.
