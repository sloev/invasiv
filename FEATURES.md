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

## Roadmap
* [ ] Make surface and instance IDs editable
* [ ] ROLE_MASTER_PERFORM: introduce states that can hold all the settings of invasis
* [ ] ROLE_MASTER_PERFORM: then introduce triggers where a trigger can trigger a state transition
* [ ] ROLE_MASTER_PERFORM: edit map input like computer keyboard and osc to triggers 
