# Resident Evil 2 frame analysis

Game: Steam, re2.exe (RT version), D3D12, 3840x2160 HDR10 (rgb10a2, colour space 3), ReShade
6.8.0, REFramework, RenoDX (same add-on as Village). Village's analysis: VILLAGE-FINDINGS.md.
VillageDLSS's shader ids don't match RE2 (no DLSS ran with the fork as it was).

## Post chain (captures: frame-1 = 100%, frame-2 = about 50%, 1920x1080 -> 3840x2160)

All at render size, in order:

| Pass | Shader | Reads | Writes |
|---|---|---|---|
| colour compose | pse9c57eca | t0 rgba16f | colour #de70a rgba16f, r8 mask |
| **TAA** | **ps89b84994** | t0 r8 mask, **t1 depth** r32g8x24, **t2 colour**, **t3 history**, **t4 velocity** rgba16snorm; root CBVs b0 (SceneInfo), b1 | rgba16f #dec6b |
| history copy | copy | TAA output | history |
| motion blur | ps1d41534e | t0 60x34 tiles, t1 1920x1080 tiles (fmt31), **t2 TAA output** | #de70a |
| bloom down/up | ps4f9b2ee4, ps2f250338 x6, ps85f5ecb8 .. ps8c6acf6e | #de70a | 960x540 .. 15x8 |
| bloom composite | psdd975721 | t0 bloom 960x540 | into #de70a (additive) |
| **tonemap** | **ps0936c4dc** (b4..b10 CBVs, LUTs) | **t0 colour** #de70a | #dec6b rgba16f |
| **stretch to output** | **csa2614017** (compute, 240x135) | t0 #dec6b | **u0 3840x2160 rgba16f** #9a4c3 |
| HUD | ps170402b7 (5-index quads) | | #9a4c3 |
| output | ps6769b8ed | | back buffer rgb10a2 |

At 100% there's no stretch: the tonemap writes the 3840x2160 target itself. No separate
sharpening pass in these captures (Village had Sharpness_PS).

Differences from Village: TAA registers shifted (t1 depth, t2 colour, t3 history, t4 velocity),
a motion blur pass between TAA and bloom, the tonemap reads t0, and the final stretch is a
compute pass writing a UAV. Next: bytecode of these passes (DumpShaders) to check the velocity and
jitter conventions and how motion blur and tonemap compute screen positions.

## Shaders (docs/shaders, DumpShaders bytecode, dxc -dumpbin)

Same entry points as Village: PreTonemap2_PS (TAA, ps89b84994, + EffectMaskTex t0; velocity maths
identical: prevUV = uv + (v.x*0.5, -v.y*0.5)), NewFinal (bloom, psdd975721),
LDRPostProcess_WithTonemap (tonemap, ps0936c4dc: RE_POSTPROCESS_Color t0 loaded by pixel
position, SceneInfo), CAS_CS (FidelityFX CAS stretch + sharpen, csa2614017), plus
MotionBlur_ReconstructPS (ps1d41534e: NeighborMax and PreComputedV loaded by pixel position, so
it can't be redrawn at output size) and LightShaftBilateralBlurYPS (pse9c57eca).

0.1.0: VillageDLSS's design with RE2's ids and registers (TAA t1/t2/t4, tonemap t0). Upscaling:
DLSS -> X, NewFinal redrawn into X, tonemap redrawn at output size into Y, CAS_CS skipped and Y
drawn into its u0 (own RTV + blit). Motion blur and CAS sharpening aren't applied on the
output-size path.

## 0.1.1 (confirmed 2026-11-03)

0.1.0 drew its own output-size tonemap from the draw callback and the image came out washed out
(yellow-grey, HUD fine). RenoDX replaces the tonemap and sets its own per-draw values; RE2DLSS's
callbacks run before renodx-*'s (alphabetical), so the separate draw ran without them. 0.1.1
redirects the game's own NewFinal and tonemap draws to output size (target, viewport, t0 slot,
SceneInfo) and lets them run; the state is put back at CAS_CS. User: looks right. Log: every
DLSS mode via the dropdown (RE2DLSS.lua works in RE2): 2561x1440, 2227x1252, 1920x1080,
1278x719 -> 3840x2160 and DLAA 3840x2160, no fallbacks.

## Ray tracing chain (for DLSS Ray Reconstruction; 2026-11-09)

Shader dump with ray-traced reflections on (PCRaytracingReflection=2): bytecode and `dxc -dumpbin`
in `docs/shaders/rt` (DXIL keeps RE Engine's entry and resource names). Ray tracing is inline ray
queries in compute shaders (no DispatchRays) at **half resolution** (1920x1080 at 4K, 100%).

| Step | Shader | Name | Reads -> writes |
|---|---|---|---|
| rays | cs940560df (32400 groups) / cs88078fd1 (8160) | rayTracingRayDirection | specular / diffuse rays |
| trace | cseb3ce396 / csed7839f5 | EntryPoint (RayQuery) | payloads |
| VFX | cs5dab1903 / csddef9834 | EntryPoint | vfxDiffuse / vfxSpecular (f32) |
| shade diffuse | csfc23ed5b | rayTracingShading | -> **shY (u32) + cocg (f32)**: GI as spherical-harmonics luma + chroma |
| shade specular | cs2f1da6b8 | rayTracingShading | -> **specular (u32)**, rayDir |
| history setup | cs5cbce99d, cs1984f87a, csd2e88065 | CalcMotionNormalDepth, CalcHistory, DownsamplingBuffer | velocity, history counts/weights |
| denoise spec | cs25501ade -> cs0af21e0f -> csa4fd0f93 -> cs091f2a20 | SpatialAccumulation, MeanVariance, TemporalAccumulation, DeflickerSpecular | specular -> filterSpecular |
| denoise diffuse | csd6792963 -> cs94984fd1 | SpatialAccumulation, TemporalAccumulation | shY/cocg -> filterShY/CoCg |
| denoise VFX | cs6b883e38 -> cs04bec84e -> csc67ec41e | VFXMedianFilter, SpatialAccumulationVFX, TemporalFilterVFX | vfxSpecular |
| final filter | cs905f94bf | BilateralFilter | history ShY/CoCg/Specular -> filter* |
| **upscale + apply** | **cse120127f** (8x8 groups at full res) | **rayTracingUpscale** | t0 depth, t2 **BaseColorMetallic**, t3 **NormalXNormalYRoughnessMisc**, t4 PreGF, t5 historyShY, t6 historyCoCg, t7 historySpecular, t8 specularVFX -> **u0 Result** (read-modify-write: adds RT lighting onto the scene colour at full res) |

Each signal keeps its format through the chain, so the denoise passes can be skipped and the raw
(or spatial-only) signal handed to the upscale pass. The diffuse rays are a quarter of the specular
ones (8160 vs 32400 groups; "Sudoku" scrambled pattern), so the raw diffuse is sparse and the
spatial accumulation probably has to stay to fill it.

G-buffer (from the upscale's decode): NormalXNormalYRoughnessMisc rgb10a2 = octahedral normal in
xy (x*2-1, y*2-1, z = 1-|x|-|y|, folded when z < 0), roughness z (>= 0.01), misc w (metallic in
BaseColorMetallic.a only counts when w > 0.667). BaseColorMetallic rgba8 sRGB = base colour +
metallic. SceneInfo (cb0) layout as Village: viewProjMat 0, transposeViewMat (3x4) 64,
transposeViewInvMat 112, projElement[2] 160, projInvElements 192, viewProjInvMat 224,
prevViewProjMat 288, screenSize 368, near 392, far 396. RayTracingDenoiserConstantBufferInfo has
ResolutionRatio, ResDiffuse/ResSpecular (RT resolution).
