# Rendering a Kerr Black Hole on the GPU

I wanted to see what a spinning black hole actually looks like. Not the Interstellar version, which is gorgeous but was deliberately toned down because Kip Thorne was worried audiences would think it was wrong. The real thing, rendered from the equations.

The result is BlackHole2: a Vulkan compute shader that integrates null geodesics in Kerr spacetime and composes them with a physically-modeled accretion disk. This post covers why the physics is harder than it looks, the specific choices I made in the implementation, and a few places where I fought the math before I understood what was actually happening.

---

## Why Kerr specifically

Schwarzschild black holes are the easy case. One parameter (mass), spherical symmetry, textbook treatment everywhere. Kerr adds one more number: the spin $a$, with $0 \leq a/M \leq 1$. That single addition breaks spherical symmetry, introduces frame-dragging, shifts the location of stable orbits inward, and — most visually interesting — creates an asymmetric photon ring that is noticeably squashed on one side. Near-extremal Kerr ($a/M \approx 0.998$, the Thorne limit for astrophysically realistic black holes) is where the interesting effects become pronounced, so that's the default.

---

## The metric and its coordinates

The Kerr solution is most naturally written in Boyer-Lindquist (BL) coordinates $(t, r, \theta, \phi)$. The line element is:

$$ds^2 = -\left(1 - \frac{2Mr}{\Sigma}\right)dt^2 - \frac{4Mar\sin^2\theta}{\Sigma}\,dt\,d\phi + \frac{\Sigma}{\Delta}dr^2 + \Sigma\,d\theta^2 + \frac{A\sin^2\theta}{\Sigma}\,d\phi^2$$

where the three metric functions are:

$$\Sigma = r^2 + a^2\cos^2\theta, \quad \Delta = r^2 - 2Mr + a^2, \quad A = (r^2 + a^2)^2 - \Delta a^2\sin^2\theta$$

I work in geometrized units $G = c = 1$, so lengths are in units of $M$. The outer event horizon sits at:

$$r_+ = M + \sqrt{M^2 - a^2}$$

At $a = 0$ this collapses to $r_+ = 2M$, the familiar Schwarzschild result. At near-extremal spin the horizon shrinks to $r_+ \to M$, and the photon ring gets correspondingly closer to it.

Boyer-Lindquist coordinates have a well-known pathology: the metric component $g_{rr} = \Sigma/\Delta$ blows up at $\Delta = 0$, i.e., at the horizon. This is a coordinate singularity, not a physical one, but it wreaks havoc on numerical integrators if you let rays get too close. The fix in practice is just to terminate any ray that falls below $r_+ + \epsilon$. I use $\epsilon = 0.01M$. Rays that reach there are captured; they contribute black pixels.

---

## Geodesics as a Hamiltonian system

The clean way to integrate null geodesics in Kerr is via the Hamiltonian:

$$H = \frac{1}{2}g^{\mu\nu}p_\mu p_\nu = 0$$

The condition $H = 0$ expresses the null constraint. The canonical variables are the coordinates $x^\mu = (t, r, \theta, \phi)$ and their conjugate momenta $p_\mu$. Hamilton's equations give:

$$\frac{dx^\mu}{d\lambda} = \frac{\partial H}{\partial p_\mu}, \qquad \frac{dp_\mu}{d\lambda} = -\frac{\partial H}{\partial x^\mu}$$

where $\lambda$ is an affine parameter. Because $H$ has no explicit $t$ or $\phi$ dependence, $p_t = -E$ and $p_\phi = L_z$ are conserved exactly — they're the photon's energy and angular momentum along the spin axis, evaluated at the camera and kept constant for the entire ray. This is a huge computational win. The state vector reduces from eight components to five: $(r, \theta, \phi, p_r, p_\theta)$.

Spelling out the equations of motion:

$$\frac{dr}{d\lambda} = \frac{\Delta}{\Sigma}p_r, \qquad \frac{d\theta}{d\lambda} = \frac{1}{\Sigma}p_\theta$$

$$\frac{d\phi}{d\lambda} = \frac{1}{\Sigma}\left(\frac{aP}{\Delta} - aE + \frac{L_z}{\sin^2\theta}\right)$$

where $P = E(r^2 + a^2) - aL_z$.

The momentum derivatives are messier. For $p_r$:

$$\frac{dp_r}{d\lambda} = -\frac{1}{2\Sigma}\left[\frac{\partial \Delta}{\partial r}p_r^2 - \frac{2P(2rE)\Delta - P^2\frac{\partial\Delta}{\partial r}}{\Delta^2}\right]$$

For $p_\theta$, I compute it by explicitly differentiating the full contravariant metric components $g^{\mu\nu}$ with respect to $\theta$:

$$\frac{dp_\theta}{d\lambda} = -\frac{1}{2}\left[\frac{\partial g^{tt}}{\partial\theta}E^2 - 2\frac{\partial g^{t\phi}}{\partial\theta}EL_z + \frac{\partial g^{rr}}{\partial\theta}p_r^2 + \frac{\partial g^{\theta\theta}}{\partial\theta}p_\theta^2 + \frac{\partial g^{\phi\phi}}{\partial\theta}L_z^2\right]$$

This form is algebraically explicit and maps cleanly onto GPU registers.

---

## The Carter constant

Here's something that caught me off guard when I first read about Kerr geodesics. In Schwarzschild, you can eliminate $\theta$ motion entirely by choosing the equatorial plane. In Kerr that doesn't work; $\theta$ genuinely oscillates. But there's a hidden fourth conserved quantity:

$$Q = p_\theta^2 + \cos^2\theta\left(\frac{L_z^2}{\sin^2\theta} - a^2 E^2\right)$$

This is the Carter constant, discovered in 1968. Its existence is non-obvious — it comes from a hidden symmetry of the Kerr spacetime associated with a rank-2 Killing tensor, and there's no simple geometric picture for it the way there is for $E$ and $L_z$. What it means practically is that Kerr geodesics are completely integrable: given $E$, $L_z$, and $Q$ you know the full orbit.

For a null geodesic, $Q$ is evaluated once at the camera position from the initial $p_\theta$:

$$Q = p_\theta^2\big|_\text{cam} + \cos^2\theta_\text{cam}\left(\frac{L_z^2}{\sin^2\theta_\text{cam}} - a^2E^2\right)$$

It stays constant forever. In my integrator I don't directly enforce $Q$ conservation — the ODE system is self-consistent and it should be conserved automatically — but I do use it to enforce the $\theta$ turning-point condition. When $p_\theta$ tries to carry the ray past its allowed $\theta$ range (where $\Theta(\theta) = Q - \cos^2\theta(L_z^2/\sin^2\theta - a^2E^2) < 0$), I revert the state and flip the sign of $p_\theta$. Without that check, rays near the photon sphere drift into unphysical regions and produce garbage.

---

## Integrators: RK4 and RKF45

The shader has two integration options.

**RK4** is a fixed-step fourth-order Runge-Kutta. Clean, predictable, easy to reason about on a GPU where divergence between threads is expensive. The default step size $d\lambda = -0.05$ (negative for backward tracing) works for most scenes, but near the photon sphere at $r \approx 1.5M$ the geodesics are exponentially sensitive to initial conditions and you need much smaller steps to stay on track.

**RKF45** uses the Cash-Karp embedded 4th/5th order pair. The error estimate is:

$$\text{err} = \max\left(|r_5 - r_4|,\, |\theta_5 - \theta_4|,\, |p_{r,5} - p_{r,4}|\right) \cdot |d\lambda|$$

The step is then adjusted by:

$$d\lambda_\text{new} = d\lambda \cdot \text{clamp}\left(0.9 \cdot \left(\frac{\text{tol}}{\text{err}}\right)^{1/5},\ 0.1,\ 5.0\right)$$

The 0.9 safety factor is standard. The clamp prevents runaway step growth in flat regions. With tolerance $10^{-6}$, RKF45 produces noticeably sharper photon rings than RK4 at equivalent average step count — it automatically tightens up exactly where the geometry is doing something interesting.

There's a nuance on the GPU: the adaptive step size is an `inout float` in GLSL, so it persists between iterations for each ray. Rays near the horizon take tiny steps; rays far away take large ones. This is fine for the RKF45 path. For the main RK4 loop I handle it differently — I scale the step geometrically based on $r/r_+$, tightening near the horizon and stretching out at large radii to avoid 5000 wasted tiny steps for rays that escape to $r = 500M$.

---

## Initializing rays: the FIDO tetrad

Getting the first momentum vector right is easy to mess up. The camera sits at some BL position $(r_\text{cam}, \theta_\text{cam}, \phi_\text{cam})$ and we want to launch a photon in a given camera-frame direction. But "camera-frame direction" means something specific: it's defined in the local orthonormal frame of a ZAMO (zero angular momentum observer), what Bardeen called a FIDO.

The FIDO tetrad at any equatorial point has basis vectors:

$$e_{\hat{t}}^\mu = \left(\frac{1}{\alpha},\, 0,\, 0,\, \frac{\omega}{\alpha}\right), \quad e_{\hat{r}}^\mu = \left(0,\, \sqrt{\frac{\Delta}{\Sigma}},\, 0,\, 0\right)$$

$$e_{\hat{\theta}}^\mu = \left(0,\, 0,\, \frac{1}{\sqrt{\Sigma}},\, 0\right), \quad e_{\hat{\phi}}^\mu = \left(0,\, 0,\, 0,\, \frac{1}{\sqrt{g_{\phi\phi}}}\right)$$

where $\alpha = \sqrt{\Delta\Sigma/A}$ is the lapse function and $\omega = 2Mar/A$ is the frame-dragging angular velocity. The FIDO is dragged along with the spacetime at this $\omega$; it has zero angular momentum even though it's "rotating" relative to distant stars.

A photon arriving with camera-frame direction $\hat{d} = (d_x, d_y, d_z)$ has contravariant 4-momentum:

$$p^\mu = e_{\hat{t}}^\mu + d_z\, e_{\hat{r}}^\mu - d_y\, e_{\hat{\theta}}^\mu + d_x\, e_{\hat{\phi}}^\mu$$

Lowering with the metric gives the covariant components $(p_t, p_r, p_\theta, p_\phi)$, from which $E$, $L_z$, and $Q$ follow immediately.

---

## The accretion disk and relativistic color shifts

The accretion disk uses a Novikov-Thorne-like temperature profile. The effective temperature at radius $r$ scales roughly as:

$$T(r) \propto T_0 \cdot \left(\frac{r}{r_\text{ISCO}}\right)^{-3/4} \cdot \left(1 - \frac{1}{\sqrt{r/r_\text{ISCO}}}\right)^{1/4}$$

The $r^{-3/4}$ comes from viscous dissipation in a thin disk; the second factor enforces zero torque at the ISCO (innermost stable circular orbit), where the disk truncates. The ISCO for prograde orbits in Kerr is:

$$r_\text{ISCO} = M\left(3 + Z_2 - \sqrt{(3 - Z_1)(3 + Z_1 + 2Z_2)}\right)$$

$$Z_1 = 1 + (1 - a_*^2)^{1/3}\left[(1+a_*)^{1/3} + (1-a_*)^{1/3}\right], \quad Z_2 = \sqrt{3a_*^2 + Z_1^2}$$

where $a_* = a/M$. At $a_* = 0.998$ the prograde ISCO is at about $r \approx 1.24M$, startlingly close to the horizon. This is why near-extremal Kerr disks look different: the inner edge is much closer to the black hole, so the hottest, most strongly lensed emission is from a much tighter region.

What color does hot plasma emit? For a thermal source the answer is the Planck spectrum:

$$B_\lambda(T) = \frac{2hc^2}{\lambda^5}\frac{1}{e^{hc/\lambda kT} - 1}$$

I integrate this against the CIE 1931 color matching functions at 16 wavelength samples from 380 nm to 780 nm, then convert XYZ to linear sRGB. The result matches what human vision would perceive — plasma at 5000 K looks orange-white, at 15000 K it goes blue-white.

But there's a correction. The plasma in the disk is orbiting relativistically, and the photons climbing out of the gravitational well are both Doppler-shifted and gravitationally redshifted. The combined effect is captured by the frequency ratio:

$$g = \frac{\nu_\text{obs}}{\nu_\text{emit}} = \frac{(p_\mu u^\mu)_\text{obs}}{(p_\mu u^\mu)_\text{emit}}$$

The denominator uses the Keplerian 4-velocity of the disk fluid, $u^\mu_\text{fluid}$, evaluated where the ray hits the disk. The Keplerian angular velocity for circular orbits in Kerr is:

$$\Omega = \frac{1}{a + r^{3/2}/\sqrt{M}}$$

With $u_t$ normalized so that $g_{\mu\nu}u^\mu u^\nu = -1$, you get $u_t = 1/\sqrt{-(g_{tt} + 2g_{t\phi}\Omega + g_{\phi\phi}\Omega^2)}$.

The specific intensity transforms as $I_\nu/\nu^3 = \text{const}$ (Liouville's theorem), so the observed spectrum is a blackbody at the shifted temperature $T' = gT$, scaled in total brightness by $g^3$:

$$I_\text{obs}(\lambda) = g^3 \cdot B_\lambda(gT)$$

A $g < 1$ photon is redshifted and dimmed. $g > 1$ is blueshifted; on the approaching side of the disk (where orbital motion adds to the line-of-sight component), plasma can appear significantly brighter than its rest-frame emission suggests. This asymmetry — one side of the disk visibly brighter than the other — is one of the signature features of a real relativistic accretion disk.

---

## GPU structure

The full pipeline is three passes: a compute shader that writes to an `rgba32f` accumulation image, a fullscreen fragment shader that tonemaps it, and the GLFW/ImGui overlay on top.

The compute shader runs in 16×16 threadgroups. Each thread is one pixel, which is fine here since there's no shared memory to coordinate between threads. The only per-thread persistent state is the accumulated color and sample count, stored in the image. Progressive refinement works by running the compute shader multiple times with a different sample index each frame, averaging new samples into the existing image.

Offline mode is the same shader but run headlessly, dispatching enough frames to reach the requested sample count, then reading back the image and writing it as a 32-bit EXR. The Vulkan memory allocator (VMA) handles staging buffers.

Shaders are compiled from GLSL to SPIR-V at build time by `glslc`. The include mechanism (`GL_GOOGLE_include_directive`) lets me split the physics into separate files — `kerr.glsl`, `disk.glsl`, `blackbody.glsl`, `sky.glsl` — rather than jamming 800 lines of math into one file.

---

## What I'd do differently

The adaptive step scaling near the horizon is ad-hoc. I multiply the step by $\max(0.05,\ (r/r_+ - 1) \cdot 0.25)$ when $r < 5r_+$. It works, but a proper geodesic integrator would use a locally-adapted coordinate that removes the $\Delta^{-1}$ divergence near the horizon, like ingoing Kerr coordinates. I didn't want to rewrite the metric functions.

The `dp_r` derivative in `kerr_rhs` has an embarrassing amount of commentary from when I wasn't sure about the on-shell cancellation. The math is correct — the $\partial_r\Sigma$ terms drop out because the on-shell Hamiltonian is zero — but I rewrote it three times before I trusted it.

NanoVDB volumetric disk loading is wired up in the SSBO bindings but the full tree traversal in GLSL isn't finished. The analytic Novikov-Thorne model is good enough to see the relevant effects. The VDB path will matter when I want to render simulation output from Houdini.

---

## Further reading

The geodesic equations used here follow Carroll's *Spacetime and Geometry*, chapter 9. The Carter constant derivation and the FIDO tetrad are in Bardeen, Press & Teukolsky (1972). For the actual color pipeline, Bruneton & Combes (2013) on real-time relativistic rendering has clean derivations of the $g$-factor and why $I_\nu/\nu^3$ is the right invariant. The Novikov-Thorne disk model is in the original 1973 paper; the temperature profile I use is a simplified version that captures the qualitative behavior without the full thin-disk hydrodynamics.
