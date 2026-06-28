# blackhole4 — physically-based Kerr black hole renderer

Real-time GPU renderer for a spinning (Kerr) black hole, written in C++20 and
Vulkan 1.2. Null geodesics are integrated in Boyer-Lindquist coordinates in a
compute shader, one ray per thread, with a relativistic thin accretion disk,
optional NanoVDB volumetrics, HDR skybox far-field and ACES post-processing.

![sample render](test_render.png)

## Building

Requirements: CMake ≥ 3.24, a C++20 compiler, the Vulkan SDK (for `glslc` and
`vulkan-1`), and an internet connection on first configure (all third-party
libraries are pulled with FetchContent: GLFW, GLM, VMA, Dear ImGui, TinyEXR,
stb, and the NanoVDB headers from the OpenVDB repository).

```
cmake -S . -B build
cmake --build build --config Release
```

Targets: `blackhole4` (the renderer) and `make_test_volume` (writes a
procedural accretion-torus `.nvdb` for testing the volumetric path).
Set `-DBH_WITH_NANOVDB=OFF` to drop the NanoVDB dependency.

## Running

Interactive preview (drag to orbit, scroll to zoom, ImGui panel for all
physics/render parameters):

```
build/Release/blackhole4 --spin 0.9
```

Offline render (headless, progressive accumulation, 32-bit float EXR):

```
build/Release/blackhole4 --offline --width 1920 --height 1080 --spp 512 \
    --spin 0.95 --out render.exr
```

Volumetric gas from a NanoVDB file:

```
build/Release/make_test_volume test_volume.nvdb
build/Release/blackhole4 --vdb test_volume.nvdb
```

### OpenVDB `.vdb` files and sequences (Houdini exports)

Reading native `.vdb` files (blosc-compressed OpenVDB, the Houdini default)
requires the OpenVDB core library. Build it once into `extern/install`, then
re-run CMake — it is picked up automatically:

```
powershell -File scripts\build_openvdb.ps1   # zlib + c-blosc + oneTBB + OpenVDB
cmake -S . -B build
cmake --build build --config Release
```

`--vdb` then accepts a `.vdb` file **or a directory containing a numbered
sequence** (sorted by filename). Grids named `*dens*` / `*temp*` are used as
density and temperature; with a density-only export the gas temperature falls
back to the thin-disk Shakura-Sunyaev profile at each sample's cylindrical
radius, so the volume still glows physically.

```
# interactive: scrub the sequence with the "seq frame" slider
build/Release/blackhole4 --vdb E:\path\to\sequence_dir --vdbscale 1.0

# offline animation: one EXR per frame (out.0001.exr, out.0002.exr, ...)
build/Release/blackhole4 --offline --vdb E:\path\to\sequence_dir ^
    --seqstart 1 --seqend 240 --spp 256 --out out.exr
```

Volume options: `--vdbscale` (renderer M per VDB world unit), `--vdbyup 0|1`
(Houdini is Y-up, the renderer Z-up; default 1), `--vdbres` (dense bake
resolution, default 256), `--seqstart/--seqend/--seqstep` (1-based frame
range; offline renders the range, preview starts at `--seqstart`).

`--help` lists all options (skybox, camera, integrator, disk parameters,
validation layers, debug views).

## Physics

Geometric units G = c = M = 1; lengths in units of the gravitational radius.

- **Metric**: Kerr in Boyer-Lindquist coordinates, dimensionless spin
  a ∈ [0, 0.998]. Inverse-metric components and all their analytic r/θ
  derivatives live in `shaders/kerr.glsl` — no finite differences anywhere.
- **Geodesics**: integrated from the super-Hamiltonian
  H = ½ g^{μν} p_μ p_ν = 0. Stationarity and axisymmetry reduce the problem
  to five coupled ODEs for (r, θ, φ, p_r, p_θ); E = −p_t and L_z = p_φ are
  algebraically conserved.
- **Conserved quantities**: E, L_z and the Carter constant Q are computed at
  the camera. Q is the integration-error metric: debug view 1 renders its
  drift |Q(λ) − Q₀| in false color (`--debug 1`). Measured at 0.9 spin,
  640×360: median drift ≈ 4e-5 (RKF45, tol 1e-6) and ≈ 6e-5 (RK4).
- **Integrators** (`shaders/integrators.glsl`): fixed-step RK4 with a
  radius- and horizon-proximity-scaled step, and adaptive RKF45 with the
  Cash-Karp 5(4) embedded pair and per-component error control.
- **Camera**: rays start with unit energy in the local orthonormal frame of a
  ZAMO/FIDO (zero-angular-momentum observer) at the camera; the tetrad
  construction is in `kerr.glsl: cameraRay`.
- **Accretion disk** (`shaders/disk.glsl`): thin equatorial disk from the
  ISCO (computed per spin via Bardeen-Press-Teukolsky) to a configurable
  outer radius. The fluid follows prograde circular geodesics with Keplerian
  Ω = 1/(r^{3/2} + a); the relativistic shift factor is
  g = ν_obs/ν_em = 1/(u^t (E − Ω L)), which contains Doppler beaming,
  gravitational redshift, and time dilation in one expression.
- **Emission**: Shakura-Sunyaev temperature profile
  T(r) ∝ (r_in/r)^{3/4} (1 − √(r_in/r))^{1/4}, peak normalized to the UI
  temperature. Color comes from a CPU-baked LUT of the Planck spectrum
  integrated against the CIE 1931 color matching functions (Wyman et al.
  analytic fits) with an *absolute* radiometric scale. Because a
  Doppler-shifted blackbody is exactly a blackbody at gT, a single LUT lookup
  at gT yields the correct shifted *and* beamed radiance — no separate g³/g⁴
  factor.
- **Volumetrics** (`shaders/volume.glsl`): NanoVDB grids are loaded on the
  CPU and baked into dense 3D textures (hardware trilinear filtering); each
  geodesic step is sub-sampled in pseudo-Cartesian space with emission from
  the blackbody LUT (Doppler-shifted by the local Keplerian flow) and
  exponential extinction.
- **Far field**: rays that pass r = 1000 M moving outward sample an
  equirectangular HDR/EXR skybox along their asymptotic direction (or a
  procedural starfield if none is given).

## Architecture

```
src/vk/        Vulkan wrappers: Context (instance/device/queue/VMA),
               Swapchain (+render pass), Resources (Buffer/Image via VMA),
               Pipeline (compute + fullscreen graphics)
src/app/       Camera (orbit + ZAMO-frame basis), Config (CLI), Blackbody
               (Planck/CIE LUT), AssetLoad (HDR/EXR skybox, NanoVDB bake),
               Renderer (descriptors, accumulation, preview loop, offline EXR)
shaders/       trace.comp (per-ray kernel) + common/kerr/integrators/disk/
               env/volume includes; post.vert/post.frag (ACES + sRGB)
tools/         make_test_volume (procedural .nvdb generator)
```

- **Preview**: one compute dispatch per frame accumulates 1 jittered sample
  per pixel into an RGBA32F storage image; any parameter or camera change is
  detected by hashing the UBO and restarts accumulation. A fragment pass
  resolves (divide by sample count), applies exposure + ACES (Hill fit) and
  sRGB encoding, then ImGui draws on top.
- **Offline**: same kernel, headless context (no surface/swapchain). Samples
  are batched 16 dispatches per submit with compute→compute barriers, the
  sample index supplied by push constant, then the accumulator is read back,
  averaged and written as 32-bit float RGB EXR via TinyEXR.
