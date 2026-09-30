# BlackHole

A real-time Kerr (spinning) black hole renderer that runs entirely on the GPU. It integrates light paths through curved spacetime in a Vulkan compute shader, with an interactive preview and an offline mode that writes full-resolution 32-bit EXR files.

**Fisheye dome viewer:** https://harrison-martin.com/p/blackhole-fisheye-viewer

<img width="3840" height="2160" alt="4K render of a near-extremal Kerr black hole" src="https://github.com/user-attachments/assets/48c4038c-0993-40c2-9cf1-b4f1b7c728ff" />

[Full-resolution 4K](https://cloud.harrison-martin.com/apps/files_sharing/publicpreview/ffkFMd5KfHpi432?file=/&fileId=1024597&x=3840&y=2160&a=true&etag=248e349ae73d7ebfcc93374acc8bbed5) · [4K close-up](https://cloud.harrison-martin.com/apps/files_sharing/publicpreview/YYxL8L6WJs4Q3id?file=/&fileId=1024708&x=3840&y=2160&a=true&etag=e9e4838af21c4ad6d5dea25efe4acf93)

## Features

- Null geodesics in Boyer–Lindquist coordinates with a Hamiltonian formulation, including frame dragging and lensing from a spinning black hole
- Two integrators: fixed-step **RK4** and adaptive **RKF45** (Cash–Karp)
- Accretion disk with blackbody emission and relativistic Doppler and gravitational shift
- Optional volumetric disk from a Houdini `.vdb` (density and temperature grids)
- HDR/EXR sky panoramas
- Interactive preview window, or headless offline rendering to 32-bit EXR

## Requirements

- Vulkan 1.2 SDK (set `VULKAN_SDK`, or have `glslc` on `PATH`)
- A C++20 compiler (MSVC 2022, Clang 16+, GCC 13+)
- CMake 3.24+

GLFW, GLM, VMA, ImGui, TinyEXR, stb and NanoVDB are fetched automatically by CMake. The HDR skyboxes in `assets/skybox/` are stored with Git LFS, so run `git lfs pull` after cloning.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Shaders compile to SPIR-V as part of the build. If `glslc` isn't found, CMake warns and you'll need to compile them yourself.

For `.vdb` disk support, install OpenVDB (for example `vcpkg install openvdb:x64-windows`) and configure with `-DBH2_USE_OPENVDB=ON`.

## Usage

```bash
blackhole2 [options]
```

### Examples

Near-extremal spin, equatorial view, Milky Way background:

```bash
blackhole2 --spin 0.998 --cam-theta 90 --cam-r 20 --skybox assets/skybox/milkyway.hdr
```

High-quality 4K offline render:

```bash
blackhole2 --offline --spin 0.9 --adaptive --tolerance 1e-7 \
           --samples 256 --out-width 4096 --out-height 4096 \
           --output bh_4k.exr
```

### Options

| option | default | description |
|---|---|---|
| `--preview` | on | interactive window |
| `--offline` | | accumulate samples and write an EXR |
| `--spin <a/M>` | 0.998 | dimensionless spin, 0 to 0.998 |
| `--mass <M>` | 1.0 | mass in geometric units |
| `--cam-r <r>` | 30 | camera radial distance |
| `--cam-theta <deg>` | 80 | camera polar angle (90 = equatorial) |
| `--cam-phi <deg>` | 0 | camera azimuth |
| `--fov <deg>` | 180 | full fisheye field of view |
| `--vdb <path>` | | Houdini `.vdb` with density and temperature grids |
| `--disk-temp <K>` | 5000 | analytic disk base temperature |
| `--no-disk` | | disable the accretion disk |
| `--skybox <path>` | | HDR or EXR panorama for the background |
| `--width` / `--height` | 1024 | preview resolution |
| `--steps <n>` | 5000 | maximum integration steps per ray |
| `--step-size <s>` | 0.05 | fixed affine step size |
| `--adaptive` | | use the RKF45 integrator |
| `--tolerance <tol>` | 1e-6 | RKF45 error tolerance |
| `--out-width` / `--out-height` | 4096 | offline output resolution |
| `--samples <n>` | 64 | samples per pixel (offline) |
| `--output <path>` | output.exr | offline output file |
| `--exposure <e>` | 1.0 | tonemap exposure |
| `--gamma <g>` | 2.2 | gamma correction |

## How it works

Each pixel traces a ray backwards from the camera. The shader integrates five coupled ODEs for `(r, θ, φ, p_r, p_θ)` until the ray falls within the event horizon, escapes past the far-field radius, or hits the disk. Energy `E`, angular momentum `L_z` and the Carter constant `Q` are computed once per ray and conserved. Disk emission uses a blackbody spectrum shifted by `g = ν_obs / ν_emit`, computed from the Keplerian velocity of the orbiting gas.

Pipeline: `trace.comp.glsl` → HDR accumulation image → `tonemap.frag.glsl` → swapchain.

```
shaders/
  kerr.glsl           geodesic integrators (RK4, RKF45) and Kerr metric helpers
  trace.comp.glsl     main ray-tracing compute shader
  disk.glsl           accretion disk emission and NanoVDB sampling
  blackbody.glsl      blackbody spectrum and frequency shift
  sky.glsl            skybox sampling
  tonemap.frag.glsl   ACES tonemapping and gamma
src/
  app/                application loop, camera, offline renderer, config
  vk/                 Vulkan wrappers (instance, device, swapchain, pipelines, VMA)
  io/                 HDR loader, EXR writer, VDB loader
```

Units are geometrized (`G = c = 1`), and all lengths are in units of the black hole's mass `M`. The inner disk edge defaults to the prograde ISCO for the chosen spin.

## Contributing

Issues and pull requests are welcome. Open areas:

- finishing NanoVDB tree traversal in GLSL for volumetric disks
- horizon-penetrating (ingoing Kerr) coordinates to replace the ad-hoc step scaling near the horizon

## Further reading

The physics and implementation in detail: [Rendering a Kerr Black Hole on the GPU](https://blog.harrison-martin.com/black-holes).

## License

MIT
