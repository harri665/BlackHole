
A real-time Kerr black hole renderer running entirely on the GPU. Null geodesics are integrated in Boyer-Lindquist coordinates using a Hamiltonian formulation, with full frame-dragging and gravitational lensing from a spinning (Kerr) black hole. Two modes: an interactive preview at whatever resolution you want, and an offline path that accumulates samples and writes a full 32-bit EXR.

## Blog Post going over the creation of this project: 
> [blog.harrison-martin.com](https://blog.harrison-martin.com/black-holes)

---

<img width="3840" height="2160" alt="BlackHole 4k" src="https://github.com/user-attachments/assets/48c4038c-0993-40c2-9cf1-b4f1b7c728ff" />

[Full Resoultion 4k](https://cloud.harrison-martin.com/apps/files_sharing/publicpreview/ffkFMd5KfHpi432?file=/&fileId=1024597&x=3840&y=2160&a=true&etag=248e349ae73d7ebfcc93374acc8bbed5)



<img width="3840" height="2160" alt="Closeup 4k" src="https://github.com/user-attachments/assets/0f69e0c2-0b8a-4e22-9722-45f0b1af211e" />

[Full Resoultion 4k](https://cloud.harrison-martin.com/apps/files_sharing/publicpreview/YYxL8L6WJs4Q3id?file=/&fileId=1024708&x=3840&y=2160&a=true&etag=e9e4838af21c4ad6d5dea25efe4acf93)

## How it works

Each pixel launches a ray backward in time. The shader integrates the geodesic equations of motion — five coupled ODEs for `(r, θ, φ, p_r, p_θ)` — until the ray either falls inside the event horizon, escapes to the far-field radius, or hits the accretion disk. The conserved quantities `E`, `L_z`, and the Carter constant `Q` are computed once per ray at camera setup and carried through the integration.

The integrator options are:

- **RK4** — fixed affine-parameter steps, fast and predictable
- **RKF45** — Cash-Karp adaptive stepping with error control; tighter geodesics near the photon sphere at the cost of variable step count

Disk emission uses blackbody spectra with a relativistic frequency-shift factor `g = ν_obs / ν_emit` computed from the Keplerian 4-velocity of the orbiting fluid. Alternatively, you can load a Houdini `.vdb` file with density and temperature grids; the shader samples it via NanoVDB SSBOs.

The full pipeline: `trace.comp.glsl` → HDR accumulation image → `tonemap.frag.glsl` → swapchain.

## Requirements

- Vulkan 1.2 SDK (set `VULKAN_SDK` or have `glslc` on `PATH`)
- C++20 compiler (MSVC 2022, Clang 16+, GCC 13+)
- CMake 3.24+

Everything else — GLFW, GLM, VMA, ImGui, TinyEXR, stb, NanoVDB — is fetched automatically by CMake.

For `.vdb` file support, build with `-DBH2_USE_OPENVDB=ON` and install OpenVDB via vcpkg:

```
vcpkg install openvdb:x64-windows
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Shaders are compiled to SPIR-V as part of the build. If `glslc` isn't found, CMake will warn and you'll need to compile them manually.

## Usage

```
blackhole2 [options]
```

**Modes**
```
--preview              Interactive window (default)
--offline              Accumulate samples and write EXR
```

**Black hole**
```
--spin <a/M>           Dimensionless spin [0, 0.998]  (default: 0.998, near-extremal)
--mass <M>             Mass in geometric units         (default: 1.0)
```

**Camera** — Boyer-Lindquist coordinates
```
--cam-r <r>            Radial distance                 (default: 30)
--cam-theta <deg>      Polar angle                     (default: 80°, near equatorial)
--cam-phi <deg>        Azimuthal angle                 (default: 0°)
--fov <deg>            Full fisheye FOV                (default: 180°)
```

**Disk**
```
--vdb <path>           Houdini .vdb with density+temperature grids
--disk-temp <K>        Analytic disk base temperature  (default: 5000 K)
--no-disk              Disable the accretion disk entirely
```

**Rendering**
```
--skybox <path>        HDR or EXR panorama for background
--width / --height     Preview resolution              (default: 1024×1024)
--steps <n>            Max integration steps per ray   (default: 5000)
--step-size <s>        Fixed affine step size          (default: 0.05)
--adaptive             Switch to RKF45 adaptive integrator
--tolerance <tol>      RKF45 error tolerance           (default: 1e-6)
```

**Offline output**
```
--out-width / --out-height    Output resolution        (default: 4096×4096)
--samples <n>                 Samples per pixel        (default: 64)
--output <path>               EXR output path          (default: output.exr)
```

**Display**
```
--exposure <e>         Tonemap exposure                (default: 1.0)
--gamma <g>            Gamma correction                (default: 2.2)
```

### Example — near-extremal spin, equatorial view

```bash
blackhole2 --spin 0.998 --cam-theta 90 --cam-r 20 --skybox milkyway.hdr
```

### Example — high-quality offline render

```bash
blackhole2 --offline --spin 0.9 --adaptive --tolerance 1e-7 \
           --samples 256 --out-width 4096 --out-height 4096 \
           --output bh_4k.exr
```

## Project structure

```
shaders/
  kerr.glsl          geodesic integrator (RK4 + RKF45), Kerr metric helpers
  trace.comp.glsl    main ray-tracing compute shader
  disk.glsl          accretion disk emission + NanoVDB sampling
  blackbody.glsl     blackbody spectrum, frequency-shift (g-factor)
  sky.glsl           skybox sampling
  tonemap.frag.glsl  ACES/gamma tonemapping
  fullscreen.vert.glsl

src/
  app/               application loop, camera, offline renderer, config
  vk/                Vulkan wrappers (instance, device, swapchain, pipelines, VMA allocator)
  io/                HDR loader (stb), EXR writer (TinyEXR), VDB loader (NanoVDB/OpenVDB)
```

## Physics notes

Coordinates are Boyer-Lindquist with geometrized units `G = c = 1`. All lengths are in units of the black hole mass `M`. The integrator propagates the Hamiltonian form `H = ½ g^{μν} p_μ p_ν = 0` for null geodesics. The inner disk boundary defaults to the prograde ISCO, computed analytically from the spin parameter.

The disk emission model accounts for gravitational redshift and the Doppler shift of the orbiting plasma. For full volumetric accretion disk structure, load a `.vdb` file exported from Houdini or any tool that writes the OpenVDB format.

## License

MIT
