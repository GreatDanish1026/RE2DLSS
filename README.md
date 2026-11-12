# RE2DLSS 1.3.0

ReShade add-on for **Resident Evil 2** (DirectX 12) that replaces the game's TAA
with **NVIDIA DLSS** or **AMD FSR 3.1 / FSR 4**, and adds **DLSS or FSR frame generation** and
**DLSS Ray Reconstruction** for the ray-traced reflections and lighting. The game
only has FSR 1 (a spatial upscaler); this gives it native anti-aliasing (DLAA) and upscaling from
Quality to Performance, with the game's bloom, tonemapping (and RenoDX HDR) on top of the upscaled
image.

DLSS: NVIDIA RTX graphics card (frame generation: RTX 40/50). FSR 3.1: any DirectX 12 graphics
card; FSR 4: Radeon cards that support it (AMD's runtime picks). ReShade 6.8 or later with add-on
support, and REFramework for the render resolution dropdown.

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
- **Sharper textures at lower render sizes** (on by default): a mip bias of log2(render scale) on
  the game's material textures, as NVIDIA recommends. It follows the render resolution as it changes, without a restart.
- **DLSS model**: Automatic (default: K, M at Performance, L at Ultra Performance, as NVIDIA recommends), or K, J, L or M (DLSS 4.5) at every resolution, or NVIDIA's default.
- **Game exposure** (on by default, Advanced tab): DLSS and FSR get the picture's real exposure (the game's colour is already exposed) instead of
  estimating it themselves: steadier brightness and less ghosting when the lighting changes.
- **Effect mask** (on by default, Advanced tab): the mask the game's own anti-aliasing uses for particles, fire, sparks and other effects goes to DLSS and FSR 3.1,
  which then trust the current frame more there: less ghosting and smearing on effects.
- **Full-resolution reflections** (off by default, Settings tab): the bundled REFramework script
  makes the game ray trace its reflections at the full render resolution instead of a reduced one:
  sharper reflections, especially with Ray Reconstruction, at a GPU cost.
- **Sharpening** (Settings tab, off by default): extra contrast-adaptive sharpening of the upscaled
  image (AMD's RCAS): after DLSS and Ray Reconstruction, or FSR's own for FSR.
- **Ray Reconstruction** (DLSS only, off by default): with the game's ray tracing on,
  NVIDIA's DLSS Ray Reconstruction 310.9.16 replaces DLSS Super Resolution. It upscales and cleans up
  the remaining noise in the ray-traced reflections and lighting, on top of the game's own ray
  tracing denoiser (built from the game's G-buffer and camera). **Ray Reconstruction model**:
  NVIDIA's default, D, E or F; an override in the NVIDIA App takes precedence. The Advanced tab can
  skip parts of the game's denoiser and hand Ray Reconstruction the rays directly, for testing:
  the game traces at half resolution, which Ray Reconstruction isn't made for, so that looks
  blotchier than keeping the game's denoiser. Advanced also shows the surface pictures Ray
  Reconstruction gets (colour, reflectivity, normals, roughness). Without ray tracing it uses
  Super Resolution.

Settings are in ReShade's Add-ons tab (Home key) under RE2DLSS: what's running at the top, then
Settings, Status and Advanced tabs. They're saved in `ReShade.ini` under `[RE2DLSS]`.

## Install

**With Fluffy Mod Manager**: put `RE2DLSS-1.3.0.zip` in Fluffy Mod Manager's `Games\RE2R\Mods`
folder (or drag it onto the manager) and enable it. It installs into the game folder:

- `RE2DLSS.addon64` (the add-on), `nvngx_dlss.dll` and `nvngx_dlssd.dll` (NVIDIA's DLSS and
  Ray Reconstruction runtimes, 310.9.16)
- `streamline\` (NVIDIA's frame generation runtime, Streamline 2.14.16, with copies of
  `nvngx_dlss.dll` and `nvngx_dlssd.dll`)
- `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` and
  `amd_fidelityfx_framegeneration_dx12.dll` (AMD's FSR runtime, SDK 2.4.0)
- `reframework\autorun\RE2DLSS.lua` (sets the render scale for the render resolution dropdown)
- `RE2DLSS-README.md`, `RE2DLSS-THIRD_PARTY_NOTICES.txt`, `RE2DLSS-licenses\`

**By hand**: copy the contents of the zip's `RE2DLSS` folder into the game folder (where
`re2.exe` is), leaving out `modinfo.ini`.

Then:

1. ReShade 6.8 or later with full add-on support must be installed for `re2.exe` (DirectX 12).
2. REFramework must be installed for the render resolution dropdown (without it, use the game's
   Image Quality slider: 0.67 = Quality, 0.58 = Balanced, 0.5 = Performance).
3. In the game, keep Anti-Aliasing on TAA (DLSS/FSR replace that pass) and FidelityFX Super
   Resolution off.
4. Open ReShade (Home), Add-ons tab, RE2DLSS: upscaling is on by default (DLSS on RTX cards, FSR
   otherwise); pick a render resolution and, if you like, frame generation.

If the NVIDIA App has a DLSS model override for the game, it takes precedence over the model here.

## Notes

- Tested at 3840x2160 with HDR (RenoDX) in exclusive full screen on an NVIDIA RTX card, with
  REFramework: DLSS upscaling (Quality) with DLSS frame generation 2x, and Ray Reconstruction
  (Quality, ray-traced reflections on, models D/E/F). FSR and FSR frame generation are the same
  code as RE7DLSS 1.1.1, where they were tested.
- Works with REFramework: REFramework hooks NVIDIA's frame generation DLL in a way that would hang
  the game at launch; the add-on steps around that hook while the game's swapchain is created.
- FSR 3.1/4 and FSR frame generation through this add-on are untested on Radeon cards so far.
- A few frames during loading may show the game's own upscaling; that's expected.
- The render scale the dropdown sets is the game's Image Quality setting, so the game saves it in
  `re2_config.ini`. With upscaling off (F9) or the dropdown on "Game's Image Quality slider", the
  script puts the game's previous value back.
- `RE2DLSS.log` next to `re2.exe` records what the add-on did and why it fell back, if it did;
  `streamline\sl.log` is NVIDIA's frame generation log.

## Building

`build.ps1` builds `bin\RE2DLSS.addon64` with Visual Studio 2022's C++ tools (MSVC): `-Install`
copies it, the NVIDIA and AMD runtimes and the REFramework script into the game folder;
`-Package` makes the Fluffy Mod Manager zip in `bin\release`. The runtimes aren't in the
repository: put NVIDIA's signed `nvngx_dlss.dll` and `nvngx_dlssd.dll` 310.9.16 in `third_party\bin`, the Streamline
2.14.16 release's `bin\x64` in `third_party\streamline\bin\x64`, and AMD's signed FSR SDK 2.4.0 DLLs
(`Kits\FidelityFX\signedbin`) in `third_party\fidelityfx\bin`. `docs\FINDINGS.md` describes how the
game renders and what the add-on changes (forked from VillageDLSS; its analysis is in `docs\VILLAGE-FINDINGS.md`); `docs\FRAMEGEN.md` covers FSR and frame
generation.

## Licence

MIT (see `LICENSE`). Third-party code and NVIDIA's and AMD's runtimes: `THIRD_PARTY_NOTICES.txt`.
