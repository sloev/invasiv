# FEATURES

## Implemented
* [x] Auto discover via UDP
* [x] UDP networking via broadcast
* [x] Symmetric architecture (single binary)
* [x] Media folder synchronization
* [x] Save mapping configurations to JSON
* [x] Shared mapping setup across instances
* [x] Role-based system: toggle Master Edit, Master Performance, or Peer
* [x] Simple interface for surface management (create/delete)
* [x] Low-latency synchronization of warp edits
* [x] Automatic file distribution from Master to Peers
* [x] Real-time synchronization status in UI
* [x] Content management with automatic video registration
* [x] Specialized editing modes (Texture vs Mapping)
* [x] Bulk control point manipulation (scale/move)
* [x] Reorderable surfaces
* [x] High-performance video playback via libmpv
* [x] Spline-based warping with efficient double mesh system
* [x] Persistent fullscreen toggle
* [x] Modern Performance UI sketch implemented
* [x] Editable surface and instance IDs
* [x] State management system (save/recall full mapping/content snapshots)
* [x] Keyboard-based trigger system for state transitions
* [x] Distributed Metronome system with Tap Tempo
* [x] Video-Tempo Sync: Auto-align video loops to metronome beats via dynamic speed skewing
* [x] Integration Test Suite: automated verification of networking and binary health in CI (see /tests)
* [x] Automated Release Pipeline: AppImage bundle and raw binary artifacts generated on each release
* [x] Skewer: Rust-based rhythmic warping utility for media preparation
    * **Skewer App:** Native desktop application for marking rhythmic events on a video timeline and re-encoding with FFmpeg to create perfect loops.
    * **Skewer WASM:** Browser-based version accessible via the project page, utilizing FFmpeg.wasm for client-side media processing and command generation.
    * **Functionality:** Align video events to a regular grid by automatically calculating segment speeds (`setpts`) for re-encoding, ensuring seamless integration with the `invasiv` metronome-sync system.

## Roadmap
* [x] fix the skewer app so it can 
  * [x] load a video
  * [x] play/pause the video
  * [x] user can use a time slider to seek around in the video
  * [x] user can add/delete/drag markers  (video beat indicators) around a second timeline just below the first one 
  * [x] user can see a counter with number of videobeats 
  * [x] user can select start and end of the video by the use of a slider in both ends, if adjusting start/end of the video makes any videobeat markers outside of the selected part of the video they should be deleted
  * [x] user should be able to export the final video to a codec that is great for transmitting over network in the file sync layer + great for the pll skewer algo and great for detail and viewing
  * [x] implement export video that takes the selected part of the video and squeezes/expands each segment between a video beat to be exactly 0.5 second, then it should save afterwards
  * [x] the timeline of the video is the scrubbing now, no need for the other scrubbing fader
  * [x] add the ability to delete a specific video beat
  * [x] add buttons to move one frame back or forth in the timeframe
  * [x] Skewer: Implement "Musical Weight" for segments (integer multiplier of 0.5s).
  * [x] Skewer: Hammer & Nail UI: Tap `Space` to drop anchor (hammer) and drag to fine-tune (nail).
  * [x] Skewer: Segment Editor: A panel to set the beat count (1, 2, 4, 8) for each interval between anchors.
  * [x] Skewer: Virtual Warp Playhead: A "PREVIEW" mode that moves at a steady 120 BPM while remapping source frames.
  * [x] Skewer: Warp Intensity Heatmap: Visual color-coding on the timeline (Green = Natural, Red = Fast, Blue = Slow).
  * [x] Skewer: Automatic loop calculation: Ensure the total warped duration is a perfect multiple of 0.5s.
  * [x] Skewer: FFmpeg Command Update: Generate precise `setpts` filters using the formula `(Weight * 0.5) / SourceDuration`.
* [x] invasiv: make the project folder selector open a file picker to choose/create a project folder
* [x] invasiv: in the "media status" add a button to open the media folder
* [x] invasiv:  surface settings - > surface id: label should before field
* [x] add a toggle button to all instances FULLSCREEN  to toglle fullscreen for that peer instance remotely
* [x] iterate on how to pitch the skewer and invasiv apps to people, what is it they can and what does it bring. "why should i bother" etc. and put it in the readme for now
* [x] skewer: analyze the current design and see if there is a simpler way to present the core idea and thereby increase self explanatory design 
* [x] invasiv: when started it should look for a settings.json file in current directory or look for a "last project folder path" in the file in ~/.invasiv and if not found it should ask the user to choose/create a project folder, it should then save the path to that project folder in a setting in ~/.invasiv
* [x] invasiv: when invasiv is started it should have some help text for the first 10 seconds that tell the hotkeys including "h" to see the help text again, plus a mention about donation via invasiv.github.io
* [x] **ONNX Model Export:** Create a standalone Python script to export BeatNet's pre-trained PyTorch weights to a static `beatnet.onnx` model file for native C++ inference.
* [x] **Lookahead Neural Inference:** Integrate ONNX Runtime (C++ API). Run the BeatNet model on a dedicated background thread using a small lookahead buffer (~50ms) to maximize precision, extracting beat probabilities from spectrograms.
* [x] **Particle Filter Decoding:** Implement a lightweight particle filter (based on the BeatNet paper) in C++ to decode neural activations into stable BPM and beat timestamps.
* [x] **Tempo & Phase Soft-Sync:** Implement a smoothing mechanism in `Metronome.h` to skew the metronome phase (`delta * 0.25`), subtracting the known lookahead and hardware latency to achieve perfect real-time alignment.
* [x] **Latency Calibration UI:** Add a slider to the GUI to manually offset timestamps (-500ms to +500ms) compensating for hardware pipeline latency.
* [x] **Network Broadcast:** Transmit the smoothed BPM and phase offsets to peer nodes via `StateManager` to synchronize the global network clock.
* [x] **Toggle Beat Tracker:** Add a UI option and internal logic to turn the neural beat tracker on and off.
* [x] **Smooth Startup:** Ensure the startup of `invasiv` is smooth and loading of heavy resources (like the ONNX model) is handled asynchronously outside of the main UI thread.
* [x] the help text should fade out after max 15 seconds (including a counter that tells so)