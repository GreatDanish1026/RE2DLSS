# RE2DLSS 2.0.0

REFramework plugin for **Resident Evil 2** (DirectX 12) that replaces the game's TAA
with **NVIDIA DLSS** or **AMD FSR 3.1 / FSR 4**, and adds **DLSS or FSR frame generation** and
**DLSS Ray Reconstruction** for the ray-traced reflections and lighting. The game
only has FSR 1 (a spatial upscaler); this gives it native anti-aliasing (DLAA) and upscaling from
Quality to Performance, with the game's bloom, tonemapping (and RenoDX HDR) on top of the upscaled
image.

DLSS: NVIDIA RTX graphics card (frame generation: RTX 40/50). FSR 3.1: any DirectX 12 graphics
card; FSR 4: Radeon cards that support it (AMD's runtime picks). Needs **REFramework** (plugin
API 1.15 or later). **ReShade isn't needed any more** (2.0): RE2DLSS hooks the game's DirectX 12
calls itself. ReShade can stay installed for other add-ons such as RenoDX HDR, which still work
with it.

## What's new in 2.0

- **No ReShade needed.** RE2DLSS is now one REFramework plugin (`reframework\plugins\RE2DLSS.dll`)
  with its own DirectX 12 hooks; everything it does is unchanged. With ReShade installed (for
  RenoDX HDR, for example), the two work together.
- **Settings in REFramework's menu** (Insert), saved in `RE2DLSS.ini`; 1.x's settings come over.
- The separate `RE2DLSS_Reflex.dll` plugin is gone (it's part of `RE2DLSS.dll`).

## What it does

- **DLSS or FSR 3/4 in place of TAA** (F9 to switch on and off): DLSS Super Resolution 310.9.16,
  or AMD FSR (SDK 2.4.0: FSR 4 where the GPU supports it, FSR 3.1 elsewhere), takes the game's own
  colour, depth, motion vectors and camera jitter.
- **Upscaler**: Auto (default: DLSS on RTX cards, FSR otherwise), DLSS, or FSR 3.1 / FSR 4.
- **Render resolution**: Native (100%: DLAA, or FSR's native anti-aliasing), Quality (67%),
  Balanced (58%) or Performance (50%) set the game's render scale (its Image Quality setting) while
  upscaling is on, and keep it there if the in-game slider moves. "Game's Image Quality slider"
  leaves it to the menu. This needs REFramework: the bundled script
  `reframework\autorun\RE2DLSS.lua` sets the render scale. (Ultra Performance isn't offered: its colours
  come out wrong in RE2.)
- **Upscaling**: below 100%, DLSS/FSR render the full output resolution, and the game's
  bloom, tonemapping/colour grading (and RenoDX's HDR, if installed) run on it at full resolution. The HUD is drawn as usual. Motion blur and the game's FidelityFX CAS sharpening aren't applied to the upscaled image (at
  100% they are).
- **Frame generation**: Off (default), DLSS 2x / 3x / 4x / Dynamic (NVIDIA Streamline 2.14.16,
  RTX 40/50; 3x and 4x need an RTX 50; with NVIDIA Reflex), or FSR 2x (any DirectX 12 card). It
  uses the game's depth, motion vectors and camera, works with either upscaler (and with the
  game's own TAA), and gets the picture without the HUD so the HUD doesn't warp. Exclusive full
  screen works. Switching it on, or between DLSS and FSR, takes a game
  restart.
- **Lower latency with DLSS frame generation** (1.3.5): Reflex's wait now happens right before
  the game reads input (keyboard and mouse, controllers), through REFramework (since 2.0 inside
  RE2DLSS.dll itself), instead of after the game had already read the frame's input. In testing it
  cut the time from input to the finished frame from about 32 ms to about 19 ms (frame generation 2x)
  at the same frame rate. **NVIDIA Reflex**: On, or On + Boost (keeps the GPU's clocks up).
- **Sharper textures at lower render sizes** (on by default): a mip bias of log2(render scale) on
  the game's material textures, as NVIDIA recommends. It follows the render resolution as it changes, without a restart.
- **DLSS model**: Automatic (default: K, M at Performance, L at Ultra Performance, as NVIDIA recommends), or K, J, L or M (DLSS 4.5) at every resolution, or NVIDIA's default.
- **Game exposure** (on by default, Advanced tab): DLSS and FSR 3.1 get the picture's real exposure (the game's colour is already exposed) instead of
  estimating it themselves: steadier brightness and less ghosting when the lighting changes. FSR 4 measures its own
  (1.3.0 gave it the game's too, which crashed games on Radeon cards).
- **Effect mask** (on by default, Advanced tab): the mask the game's own anti-aliasing uses for particles, fire, sparks and other effects goes to DLSS and FSR 3.1,
  which then trust the current frame more there: less ghosting and smearing on effects.
- **Motion vectors without the camera jitter** (on by default, Advanced tab; 1.3.3): the game's motion vectors
  include each frame's camera jitter; DLSS, Ray Reconstruction, FSR and frame generation now get them without it.
  With the jitter, DLSS left soft blocky patches on still faces at Performance, and Ray Reconstruction could
  flicker on characters. A second switch keeps frame generation on the game's own motion vectors.
- **Full-resolution reflections** (off by default, Settings tab): the bundled REFramework script
  makes the game ray trace its reflections at the full render resolution instead of a reduced one:
  sharper reflections, especially with Ray Reconstruction, at a GPU cost.
- **Sharpening** (Settings tab, off by default): extra contrast-adaptive sharpening of the upscaled
  image (AMD's RCAS): after DLSS and Ray Reconstruction, or FSR's own for FSR.
- **Ray Reconstruction** (DLSS only, off by default): with the game's ray tracing on,
  NVIDIA's DLSS Ray Reconstruction 310.9.16 replaces DLSS Super Resolution. It upscales and cleans up
  the noise in the ray-traced reflections and lighting, with guides built from the game's
  G-buffer and camera. **Ray Reconstruction model**:
  NVIDIA's default, D, E or F; an override in the NVIDIA App takes precedence. By default (Auto,
  1.3.4) the add-on looks at how finely the game ray traces: at the render resolution (as with a
  Graphics Enhancement script's higher ray tracing quality) the game's frame-to-frame denoising is
  skipped and Ray Reconstruction does it; at the game's own, smaller size the rays are too sparse
  for that, so the game's denoiser stays and Ray Reconstruction cleans up after it. The Advanced tab
  can choose either always, or skip all of the game's denoiser.
  Advanced also shows the surface pictures Ray
  Reconstruction gets (colour, reflectivity, normals, roughness). Without ray tracing it uses
  Super Resolution.

Settings are in REFramework's menu (Insert key) under **RE2DLSS**: what's running at the top, then
Settings, Status and Advanced tabs. They're saved in `RE2DLSS.ini` next to `re2.exe` (2.0 copies
1.x's settings from `ReShade.ini`'s `[RE2DLSS]` section the first time).

## Install

**Updating from 1.x**: uninstall RE2DLSS 1.x first (in Fluffy Mod Manager, or delete
`RE2DLSS.addon64` and `reframework\plugins\RE2DLSS_Reflex.dll`). While `RE2DLSS.addon64` is still
there, 2.0 stays off and says so in its menu section, so the two never run together.

**With Fluffy Mod Manager**: put `RE2DLSS-2.0.0.zip` in Fluffy Mod Manager's `Games\RE2R\Mods`
folder (or drag it onto the manager) and enable it. It installs into the game folder:

- `reframework\plugins\RE2DLSS.dll` (the plugin: everything RE2DLSS does, including Reflex's
  lower-latency timing)
- `nvngx_dlss.dll` and `nvngx_dlssd.dll` (NVIDIA's DLSS and Ray Reconstruction runtimes, 310.9.16)
- `streamline\` (NVIDIA's frame generation runtime, Streamline 2.14.16, with copies of
  `nvngx_dlss.dll` and `nvngx_dlssd.dll`)
- `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` and
  `amd_fidelityfx_framegeneration_dx12.dll` (AMD's FSR runtime, SDK 2.4.0)
- `reframework\autorun\RE2DLSS.lua` (sets the render scale for the render resolution dropdown)
- `RE2DLSS-README.md`, `RE2DLSS-THIRD_PARTY_NOTICES.txt`, `RE2DLSS-licenses\`

**By hand**: copy the contents of the zip's `RE2DLSS` folder into the game folder (where
`re2.exe` is), leaving out `modinfo.ini`.

Then:

1. REFramework must be installed for `re2.exe` (its `dinput8.dll`; plugin API 1.15 or later, as
   in current builds). RE2DLSS loads as its plugin.
2. In the game, keep Anti-Aliasing on TAA (DLSS/FSR replace that pass) and FidelityFX Super
   Resolution off.
3. Open REFramework's menu (Insert), RE2DLSS: upscaling is on by default (DLSS on RTX cards, FSR
   otherwise); pick a render resolution and, if you like, frame generation.

If the NVIDIA App has a DLSS model override for the game, it takes precedence over the model here.

## Notes

- 2.0 tested at 3840x2160 in exclusive full screen on an NVIDIA RTX card, both without ReShade and
  with ReShade + RenoDX HDR installed: DLSS Ray Reconstruction (DLAA to Performance), DLSS frame
  generation 2x, Reflex at input, the settings in REFramework's menu. FSR and FSR frame generation
  are the same code as 1.x; under 2.0 they're untested so far.
- 2.0 is new code under the hood (its own DirectX 12 hooks in place of ReShade's): if something
  that worked in 1.3.5 doesn't, `RE2DLSS.log` next to `re2.exe` is the place to look, and 1.3.5
  can still be installed instead.
- Works with REFramework: REFramework hooks NVIDIA's frame generation DLL in a way that would hang
  the game at launch; the add-on steps around that hook while the game's swapchain is created.
- FSR 3.1/4 and FSR frame generation through this add-on are untested on Radeon cards so far.
- AMD Radeon (DirectX 12): 1.3.4 finds the camera constants on Radeon systems too (fix contributed by a Radeon user, RE7DLSS pull request #3); before, some Radeon systems reported "camera constants (SceneInfo) not found" and the add-on stayed off. Not tested on Radeon here.
- A few frames during loading may show the game's own upscaling; that's expected.
- The render scale the dropdown sets is the game's Image Quality setting, so the game saves it in
  `re2_config.ini`. With upscaling off (F9) or the dropdown on "Game's Image Quality slider", the
  script puts the game's previous value back.
- `RE2DLSS.log` next to `re2.exe` records what the add-on did and why it fell back, if it did;
  `streamline\sl.log` is NVIDIA's frame generation log.

## Building

`build.ps1` builds `bin\RE2DLSS.dll` (the REFramework plugin) with Visual Studio 2022's C++ tools
(MSVC): `-Install` copies it, the NVIDIA and AMD runtimes and the REFramework script into the game
folder (switching off a 1.x install there);
`-Package` makes the Fluffy Mod Manager zip in `bin\release`. The runtimes aren't in the
repository: put NVIDIA's signed `nvngx_dlss.dll` and `nvngx_dlssd.dll` 310.9.16 in `third_party\bin`, the Streamline
2.14.16 release's `bin\x64` in `third_party\streamline\bin\x64`, and AMD's signed FSR SDK 2.4.0 DLLs
(`Kits\FidelityFX\signedbin`) in `third_party\fidelityfx\bin`. `src\rdx` is the DirectX 12 hook
layer that serves ReShade's add-on API in place of ReShade (`probe\FINDINGS.md`: why a REFramework
plugin can do this). `docs\FINDINGS.md` describes how the
game renders and what the add-on changes (forked from VillageDLSS; its analysis is in `docs\VILLAGE-FINDINGS.md`); `docs\FRAMEGEN.md` covers FSR and frame
generation.

## Licence

MIT (see `LICENSE`). Third-party code and NVIDIA's and AMD's runtimes: `THIRD_PARTY_NOTICES.txt`.
