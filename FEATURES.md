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

## Roadmap
* [ ] ROLE_MASTER_PERFORM: expand triggers to OSC and MIDI input
* [ ] ROLE_MASTER_PERFORM: add transition times (fade) between states
* [ ] ROLE_MASTER_PERFORM: add audio analysis triggers (beat/envelope)
