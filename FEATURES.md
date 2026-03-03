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
* [ ] fix the skewer app so it can 
  * [ ] load a video
  * [ ] play/pause the video
  * [ ] user can use a time slider to seek around in the video
  * [ ] user can add/delete/drag markers  (video beat indicators) around a second timeline just below the first one 
  * [ ] user can see a counter with number of videobeats 
  * [ ] user can select start and end of the video by the use of a slider in both ends, if adjusting start/end of the video makes any videobeat markers outside of the selected part of the video they should be deleted
  * [ ] user should be able to export the final video to a codec that is great for transmitting over network in the file sync layer + great for the pll skewer algo and great for detail and viewing
* [ ] invasiv: make the project folder selector open a file picker to choose/create a project folder
* [ ] invasiv: in the "media status" add a button to open the media folder
* [ ] invasiv:  surface settings - > surface id: label should before field
* [ ] add a toggle button to all instances FULLSCREEN  to toglle fullscreen for that peer instance remotely
