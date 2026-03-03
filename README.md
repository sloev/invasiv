![demo time](./docs/screencast.gif)

# Invasiv // Rhythmic Projection Mapping

**Invasiv** is a high-performance VJ and projection mapping ecosystem designed for rhythmic precision and effortless distributed performance.

### Why Invasiv?
Most mapping software treats networking as an afterthought. Invasiv treats it as the foundation.
- **No Master/Slave Complexity**: Every instance is the same. Toggle roles on the fly. Sync one machine or twenty with a single broadcast.
- **Automatic Orchestration**: Master machines automatically distribute media and mapping configurations to peers. Stop manually copying files over USB sticks.
- **Rhythmic Soul**: Deep integration with a distributed metronome. Your visuals don't just "play"—they dance.

### Why Skewer?
Skewer is the companion tool that makes your media musical.
- **Frame-Perfect Rhythms**: Mark visual "anchors" (like a hammer hitting a nail) and let Skewer warp the video segments between them to fit a perfect musical grid.
- **Metronome Ready**: Exported loops are optimized for Invasiv's dynamic speed-skewing engine, ensuring your video events land exactly on the beat, every single time.

## Key Features
- **Symmetric Architecture**: Run the same binary on all machines; toggle roles (Master/Peer) on the fly.
- **Dynamic Warping**: Advanced spline-based mesh manipulation for complex surfaces.
- **Network Sync**: Automatic peer discovery and media synchronization over UDP broadcast.
- **Modern Performance**: High-efficiency rendering with libmpv integration for video playback.

Full feature list available in [FEATURES.md](./FEATURES.md).

## Ecosystem

- **Invasiv** ![Invasiv Version](https://img.shields.io/github/v/release/invasiv/invasiv.github.io?filter=invasiv-*&label=latest&color=red): The main VJ engine. Distributed, symmetric, and fast.
- **Skewer** ![Skewer Version](https://img.shields.io/github/v/release/invasiv/invasiv.github.io?filter=skewer-*&label=latest&color=blue): A dedicated Rust-based utility for rhythmic media preparation. 
  - **Native**: Requires `ffmpeg` installed on your system for preview and export.
  - **Web**: Uses `ffmpeg.wasm` for browser-based rhythmic planning.

**Compatibility**: Both tools require Protocol V1 for network sync.

## Installation

### Releases (Recommended)
Download the latest **Invasiv-x86_64.AppImage** from the [Releases](https://github.com/invasiv/invasiv.github.io/releases) page. This is a standalone bundle that includes all necessary dependencies.

```bash
chmod +x Invasiv-x86_64.AppImage
./Invasiv-x86_64.AppImage
```

### Prerequisites (for building/raw binary)
- **libmpv2**: Required for Invasiv video playback.
- **ffmpeg**: Required for Skewer media preparation.

```bash
sudo apt install libmpv-dev ffmpeg
```

### Building (Docker - Recommended)
The easiest way to build Invasiv is using Docker. You can pull a pre-compiled base image to save about 10-15 minutes of build time:

```bash
make pull-base  # (Optional) Pulls pre-compiled openFrameworks core from GHCR
make build      # Compiles the application source
make extract    # Copies the binary to ./artifacts
```
The compiled binary will be available in the `artifacts/` directory.

## Development

The project is structured as an openFrameworks app:
- `invasiv_app/src`: Core C++ source code.
- `invasiv_app/addons.make`: List of required oF addons.

## License
Distributed under the MIT License. See `LICENSE` for more information.

### Attributions
- **openFrameworks**: [MIT License](https://github.com/openframeworks/openFrameworks)
- **ofxMPVPlayer**: Pete Haughie
- **ofxImGui**: [MIT License](https://github.com/jvcleave/ofxImGui)
